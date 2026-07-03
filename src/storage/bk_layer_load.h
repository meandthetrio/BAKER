#pragma once

#include <cstdint>

#include "bk_file_format.h"

struct AppSharedState;

namespace bk {

// Build layer-B slice handles from an already-loaded PCM blob in
// SdBkLayerBBuffer(), then publish slot 0 and set loaded=true. `pcm_offset_frames`
// is where the PCM starts in the buffer (0 for the sync loader that reads PCM to
// buffer[0]; kHeaderSize/2 for the async loader that reads the whole file from
// offset 0, leaving the header in front of the PCM). Returns false on a bad slice
// count. Main thread only.
bool BkLayer_FinalizeLayerB(AppSharedState& shared,
                            const BkFileHeader& hdr,
                            uint32_t pcm_offset_frames);

// Synchronously load a .bk file at `path` into layer B's playback slot.
// Parses + validates the .bk header, streams the PCM blob into the SDRAM
// `g_sd_bk_layer_b_buf` (via BkRead_LoadIntoBuffer), populates
// shared.bk_layer_b with the loaded header and a per-slice Sample handle
// array (each Sample's root_key == its MIDI note, so the voice engine
// plays each slice at native pitch). Sets `loaded = true` on success.
//
// On failure, leaves `loaded = false` and returns false; the existing
// state is otherwise untouched.
//
// Single-threaded, main thread only. NOT safe to call from audio interrupt.
// Caller is responsible for ensuring no MIDI notes will be triggered against
// the slot while this runs.
bool BkLayer_LoadIntoLayerB(const char* path, AppSharedState& shared);

// Clear layer B's .bk slot. After this, NoteOn for layer B falls back to
// the normal .wav sampler path. Called when user picks a .wav for layer B
// (the .bk override should yield) and when the slot must be invalidated.
void BkLayer_ClearLayerB(AppSharedState& shared);

} // namespace bk
