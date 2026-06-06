#pragma once

#include <cstdint>

// Shared loop-seam bake helpers. Pre-render the loop crossfade into a PCM copy so
// the runtime can play a seamless loop with a plain single-fetch read (the runtime
// seam crossfade is then suppressed for that layer via SetLayerSeamBaked).
//
// Used by the loader (bake on WAV load) and the ADSR seam-edit screen (re-bake on
// commit). Reuses the pure blend math in voice_engine_render_fetch.cpp.

// Default loop seam crossfade length as a fixed number of frames (~21 ms @ 48 kHz),
// independent of the loaded file length. Converted to the engine's 0..0.5 fraction
// model against the loop region.
static constexpr uint32_t kDefaultSeamCrossfadeFrames = 1024u;

// Converts a fixed default seam frame count into the engine's 0..0.5 fraction for
// the given loop region [start,end).
float DefaultSeamCrossfadeAmount(uint32_t start, uint32_t end);

// Copies raw [0,length) into dst, then overwrites the loop seam region
// [end-seam, end) with the pre-blended crossfade so the loop is seamless in PCM.
// Returns true if a non-zero seam was baked. Safe to run on any non-audio thread;
// the caller must ensure dst is not being read by the audio thread during the bake.
bool BakeLoopSeamToBuffer(const int16_t* raw,
                          uint32_t length,
                          uint32_t start,
                          uint32_t end,
                          float amount,
                          float shape,
                          float sample_rate,
                          int16_t* dst);
