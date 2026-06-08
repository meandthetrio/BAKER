#include "bk_layer_load.h"

#include "app_state_shared.h"
#include "bk_file_format.h"
#include "bk_file_reader.h"
#include "sd_sample_pool.h"

namespace bk {

bool BkLayer_LoadIntoLayerB(const char* path, AppSharedState& shared)
{
    // Clear first so a partial / failed load leaves the slot empty rather
    // than half-populated.
    shared.bk_layer_b.loaded = false;

    BkFileHeader hdr{};
    int16_t* const pcm_buf = SdBkLayerBBuffer();
    if(!BkRead_LoadIntoBuffer(path, pcm_buf, kBkLayerBMaxFrames, hdr))
        return false;

    // Defensive: the reader already validated lo/hi and per-slice frame
    // count, but double-check the slice count matches the layout we expect.
    const uint32_t slice_count = static_cast<uint32_t>(hdr.hi_note - hdr.lo_note + 1u);
    if(slice_count == 0u || slice_count > bk::kSliceCount)
        return false;

    // Pre-build 85 Sample handles, one per semitone in [lo_note, hi_note].
    // Each Sample's root_key == its MIDI note so the voice engine computes
    // pitch ratio = 2^((note - root_key)/12) = 1.0 → native-pitch playback.
    for(uint32_t i = 0u; i < slice_count; ++i)
    {
        Sample& s         = shared.bk_layer_b.slice_sample[i];
        s.pcm             = pcm_buf + (i * hdr.source_duration_samples);
        s.length          = hdr.source_duration_samples;
        s.sample_rate     = 48000;
        s.root_key        = static_cast<uint8_t>(hdr.lo_note + i);
        s.loop_start      = 0;
        s.loop_end        = hdr.source_duration_samples;
        s.loop_enabled    = false;
    }
    // Any unused entries (shouldn't happen with slice_count == kSliceCount,
    // but defensive) get zeroed so a stale slot pointer can't sneak through.
    for(uint32_t i = slice_count; i < bk::kSliceCount; ++i)
    {
        shared.bk_layer_b.slice_sample[i] = Sample{};
    }

    shared.bk_layer_b.hdr    = hdr;
    shared.bk_layer_b.loaded = true;

    // Publish the root-note slice as layer B's "current sample" so the
    // main-thread MIDI handler ([main.cpp HandleMidiNoteOn]) doesn't skip
    // layer B for empty-pcm reasons, and the engine screen renders the
    // loaded name instead of "no sample". The voice engine's bk_layer_b
    // override fires first inside ProcessEvents, so this Sample is only a
    // "is there something to play" signal — actual playback uses the
    // per-note slice from `shared.bk_layer_b.slice_sample`, not this one.
    const uint32_t root_slice_idx
        = (hdr.root_midi_note >= hdr.lo_note && hdr.root_midi_note <= hdr.hi_note)
              ? static_cast<uint32_t>(hdr.root_midi_note - hdr.lo_note)
              : 0u;
    shared.sample.publish.sd_slots[1] = shared.bk_layer_b.slice_sample[root_slice_idx];
    return true;
}

void BkLayer_ClearLayerB(AppSharedState& shared)
{
    shared.bk_layer_b.loaded = false;
}

} // namespace bk
