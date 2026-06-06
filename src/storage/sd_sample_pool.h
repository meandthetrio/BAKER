#pragma once

#include <cstdint>

#include "storage_limits.h"

static constexpr uint32_t kSdSampleMaxFrames = 240000; // 5 seconds @ 48kHz
// Playback slot 0 lives in fast internal RAM_D2 (256K), so it is capped to what
// fits there: 131072 frames * 2 bytes = 256K (~2.73 s @ 48kHz). Slot 1 and the
// record/manage/baked buffers stay in SDRAM at kSdSampleMaxFrames.
static constexpr uint32_t kSdPlaybackD2MaxFrames = 131072;

// Buffer the engine reads (the *baked* playback buffer). For slot 0 that's a
// RAM_D2 buffer; for slot 1 it's the SDRAM "baked" buffer.
int16_t* SdSampleBuffer(uint8_t slot);
// Where the SD loader should DMA chunks for a given slot. Both slots stage in
// SDRAM (DMA-reachable), then BakeLoopSeamToBuffer renders raw -> playback.
int16_t* SdSampleLoadBuffer(uint8_t slot);
// Persistent raw reference, kept past load so the seam edit screen can re-bake
// with new params. Same buffer as SdSampleLoadBuffer for each slot.
int16_t* SdSampleRawBuffer(uint8_t slot);
int16_t* SdRecordBuffer();
int16_t* SdManageBuffer();
// Per-layer scratch holding the seam-baked copy of a loaded sample. The raw load
// stays in SdSampleBuffer(slot) for future re-bakes; the baked copy is what plays.
int16_t* SdBakedBuffer(uint8_t slot);
// Max loadable frames for a given playback slot (slot 0 = RAM_D2 cap, else SDRAM).
uint32_t SdSampleMaxFrames(uint8_t slot);
