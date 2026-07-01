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

// Formant preservation flag, driven by the Settings/Shift toggle via
// SetPreserveFormants(). Re-applied on every RunPitchShift so a mid-session
// toggle takes effect on the next bake. Default on.
bool    s_preserve_formants = true;

// Float scratch buffers live in SDRAM (overwritten before read; SDRAM is
// initialized by the time the first RunPitchShift call happens, well after
// main() has called hw.Init()).
//
// kMaxLatencyPad: signalsmith-stretch is STFT-based and has internal
// latency. The total input→output delay is inputLatency() (analysis
// half) + outputLatency() (synthesis half), each ~half a block. After
// reset(), the first `inputLatency + outputLatency` output samples are
// warmup silence — the algorithm hasn't accumulated enough input to
// emit real audio yet. At presetDefault (block=5760, interval=1440 @
// 48k) the combined latency is roughly the full block (~5760). We size
// the input/output buffers generously past kMaxFrames so we can: (a) pad
// the input with silence to flush the tail past the source end, and (b)
// hold the over-length output before shifting it left by that combined
// latency. 16384 frames = 340 ms — safely above any realistic value.
static constexpr uint32_t kMaxLatencyPad = 16384u;

ADSR2_SECTION(".sdram_bss") float s_float_in [kMaxFrames + kMaxLatencyPad];
ADSR2_SECTION(".sdram_bss") float s_float_out[kMaxFrames + kMaxLatencyPad];

void EnsureConfigured()
{
    if(s_configured)
        return;
    // presetDefault: block 5760 / interval 1440 at 48 kHz mono. Larger
    // block + tighter interval than presetCheaper (4800/1920) — more
    // overlap per output sample, cleaner harmonic reconstruction on tonal
    // slices. Heap cost (~100-200 KB of std::vector inside configure())
    // is absorbed by the SDRAM heap installed via the _sbrk override in
    // main.cpp; the AXI-SRAM-too-small constraint that originally forced
    // presetCheaper no longer applies.
    s_stretch.presetDefault(/*channels*/ 1, /*sampleRate*/ 48000.0f);

    // Formant preservation. A plain phase-vocoder transposition drags the
    // spectral envelope (the instrument's fixed body/formant resonances) along
    // with the pitch, so large shifts sound "chipmunk" (up) or "dark/monster"
    // (down). With compensatePitch=true the library measures the source's
    // envelope and re-applies it AFTER the pitch map, so the harmonics move but
    // the timbre stays put — the standard multisampler behaviour, and the
    // single biggest naturalness win for the wide (multi-octave) bake slots.
    // Only the transposed slices pay for it; the root slot is a straight copy.
    // Runtime-controlled via the Settings/Shift toggle (s_preserve_formants);
    // re-applied per-call in RunPitchShiftChunked so toggling takes effect.
    s_stretch.setFormantFactor(1.0f, /*compensatePitch=*/s_preserve_formants);

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

void SetPreserveFormants(bool on)
{
    s_preserve_formants = on;
}

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
    // Honour the current formant-preservation setting (cheap; lets a Settings
    // toggle take effect without reconfiguring the engine).
    s_stretch.setFormantFactor(1.0f, /*compensatePitch=*/s_preserve_formants);
    s_stretch.reset();
    s_stretch.setTransposeSemitones(static_cast<float>(semitones));

    // Query the FULL input-to-output latency. inputLatency() is the
    // analysis half (block centre vs input cursor); outputLatency() is the
    // synthesis half (block centre vs emitted output). Their sum is what
    // we have to shift by — using outputLatency() alone leaves ~half a
    // block of leading silence on every pitched slice.
    uint32_t latency = static_cast<uint32_t>(s_stretch.inputLatency())
                       + static_cast<uint32_t>(s_stretch.outputLatency());
    if(latency > kMaxLatencyPad)
        latency = kMaxLatencyPad; // clip rather than corrupt — output start
                                  // will still be aligned to within
                                  // (real latency - kMaxLatencyPad) samples
    const uint32_t total_in_frames  = frames + latency;
    const uint32_t total_out_frames = frames + latency;

    Int16ToFloat(source, s_float_in, frames);
    // Pad input past source end with silence to flush the algorithm's
    // internal buffer — the pad samples drive out the trailing pitched
    // audio that would otherwise be stuck inside.
    std::memset(s_float_in + frames, 0, sizeof(float) * latency);
    std::memset(s_float_out, 0, sizeof(float) * total_out_frames);

    // Chunk the process() call into kPsolaChunks pieces over the padded
    // input length. The algorithm's internal state carries across calls
    // (we only reset() once at the top), so consecutive chunks pick up
    // where the previous left off. Each chunk is
    // (total_in_frames / kPsolaChunks); any remainder goes into the final
    // chunk so we cover the full padded source.
    const uint32_t base_chunk_frames = total_in_frames / kPsolaChunks;
    uint32_t       offset            = 0;
    for(uint8_t c = 0; c < kPsolaChunks; ++c)
    {
        if(cb != nullptr)
            cb(c, kPsolaPhaseLabel[c]);

        uint32_t chunk_frames = base_chunk_frames;
        if(c == kPsolaChunks - 1u)
            chunk_frames = total_in_frames - offset; // last chunk absorbs remainder
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

    // Shift the output left by `latency` samples so the pitched audio is
    // aligned to sample 0 (matches the root slice's unprocessed start).
    // Without this every pitched slice would lead with ~60 ms of silence
    // and play noticeably late in an arp against the root.
    FloatToInt16(s_float_out + latency, dest, frames);
    return true;
}

} // namespace bake
