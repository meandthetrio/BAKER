#include "bake_psola.h"

#include "mem_regions.h"

// Standard headers FIRST. signalsmith-linear/fft.h uses `std::memcpy`
// without including <cstring> itself — a portability bug in that library
// that bites compilers where <vector>/<complex> don't transitively bring
// in <cstring>. Including it here pulls std::memcpy into scope before the
// signalsmith headers are parsed.
#include <algorithm>
#include <cstring>

// signalsmith-stretch pulls in <vector>, <array>, <random>, etc. Heavy STL
// inclusion is unavoidable; the file lives behind this single TU so the rest
// of the project doesn't pay the include-cost everywhere.
#include "signalsmith-stretch/signalsmith-stretch.h"

namespace bake {

namespace {

using Stretch = signalsmith::stretch::SignalsmithStretch<float>;

// SignalsmithStretch object lives in default .bss (AXI SRAM, zeroed at
// startup). It contains std::vector members whose initial state must be
// "empty / null" — default-constructed vectors satisfy that, and BSS zero-
// init keeps it that way until first configure(). Not placing in .sdram_bss
// because the SDRAM controller is initialized by libDaisy's hw.Init() inside
// main(), which runs AFTER C++ static constructors; an .sdram_bss instance
// could have its constructor write to inaccessible memory.
Stretch s_stretch;
bool    s_configured = false;

// Float scratch buffers live in SDRAM (overwritten before read; SDRAM is
// initialized by the time the first RunPitchShift call happens, well after
// main() has called hw.Init()).
ADSR2_SECTION(".sdram_bss") float s_float_in[kMaxFrames];
ADSR2_SECTION(".sdram_bss") float s_float_out[kMaxFrames];

void EnsureConfigured()
{
    if(s_configured)
        return;
    // presetCheaper uses smaller internal buffers (block 4800 vs 5760 at
    // 48 kHz mono) and enables splitComputation by default. Reduces the
    // heap footprint of the std::vector allocations inside configure() —
    // important on this device because (a) the AXI SRAM heap is not huge
    // and (b) -fno-exceptions makes a failed allocation an immediate
    // hardfault, observed as the bake function freezing mid-progress.
    // Quality cost is small; for a one-time offline bake the trade is
    // clearly in favor of "fits". Revisit with presetDefault + a custom
    // SDRAM allocator if quality proves inadequate.
    s_stretch.presetCheaper(/*channels*/ 1, /*sampleRate*/ 48000.0f);
    s_stretch.setFormantFactor(1.0f);
    s_configured = true;
}

void Int16ToFloat(const int16_t* src, float* dst, uint32_t n)
{
    constexpr float kScale = 1.0f / 32768.0f;
    for(uint32_t i = 0; i < n; ++i)
        dst[i] = static_cast<float>(src[i]) * kScale;
}

void FloatToInt16(const float* src, int16_t* dst, uint32_t n)
{
    for(uint32_t i = 0; i < n; ++i)
    {
        float v = src[i];
        if(v > 1.0f)        v = 1.0f;
        else if(v < -1.0f)  v = -1.0f;
        // 32767 not 32768: avoid +1.0f -> 0x8000 overflow which would wrap
        // around to a large negative when stored.
        dst[i] = static_cast<int16_t>(v * 32767.0f);
    }
}

} // namespace

bool RunPitchShift(const int16_t* source,
                   uint32_t       frames,
                   int            semitones,
                   int16_t*       dest)
{
    return RunPitchShiftChunked(source, frames, semitones, dest, nullptr);
}

// Phase labels cycled through during chunked progress reporting. Five
// plausibly-named PSOLA stages — internally signalsmith does all of these
// inside a single process() call, but cycling the label across chunks
// gives the user something to look at while the bake works.
const char* const kPsolaPhaseLabel[kPsolaChunks] = {
    "psola: setup",
    "psola: analysis",
    "psola: pitch shift",
    "psola: synthesis",
    "psola: finalize",
};

bool RunPitchShiftChunked(const int16_t*  source,
                          uint32_t        frames,
                          int             semitones,
                          int16_t*        dest,
                          PsolaProgressCb cb)
{
    if(source == nullptr || dest == nullptr)
        return false;
    if(frames == 0u || frames > kMaxFrames)
        return false;
    if(semitones < -kMaxSemitones || semitones > kMaxSemitones)
        return false;

    EnsureConfigured();
    s_stretch.reset();
    s_stretch.setTransposeSemitones(static_cast<float>(semitones));

    Int16ToFloat(source, s_float_in, frames);
    std::memset(s_float_out, 0, sizeof(float) * frames);

    // Chunk the process() call into kPsolaChunks pieces. The algorithm's
    // internal state carries across calls (we only reset() once at the top),
    // so consecutive chunks pick up where the previous left off. Each chunk
    // is (frames / kPsolaChunks); any remainder goes into the final chunk
    // so we cover the full source.
    const uint32_t base_chunk_frames = frames / kPsolaChunks;
    uint32_t       offset            = 0;
    for(uint8_t c = 0; c < kPsolaChunks; ++c)
    {
        if(cb != nullptr)
            cb(c, kPsolaPhaseLabel[c]);

        uint32_t chunk_frames = base_chunk_frames;
        if(c == kPsolaChunks - 1u)
            chunk_frames = frames - offset; // last chunk absorbs remainder
        if(chunk_frames == 0u)
            continue;

        float* in_ptr  = s_float_in + offset;
        float* out_ptr = s_float_out + offset;
        float* in_ch[1]  = { in_ptr };
        float* out_ch[1] = { out_ptr };
        s_stretch.process(in_ch,
                          static_cast<int>(chunk_frames),
                          out_ch,
                          static_cast<int>(chunk_frames));
        offset += chunk_frames;
    }

    FloatToInt16(s_float_out, dest, frames);
    return true;
}

} // namespace bake
