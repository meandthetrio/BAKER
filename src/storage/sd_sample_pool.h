#pragma once

#include <cstdint>

#include "storage_limits.h"

static constexpr uint32_t kSdSampleMaxFrames = 240000; // 5 seconds @ 48kHz
// Playback slot 0 lives in fast internal RAM_D2 (256K), so it is capped to what
// fits there: 131072 frames * 2 bytes = 256K (~2.73 s @ 48kHz). Slot 1 and the
// record/manage/baked buffers stay in SDRAM at kSdSampleMaxFrames.
static constexpr uint32_t kSdPlaybackD2MaxFrames = 131072;

int16_t* SdSampleBuffer(uint8_t slot);
// Where the SD loader should DMA chunks for a given slot. Slot 0 plays from
// RAM_D2, which SDMMC's DMA cannot reach, so it stages in an SDRAM buffer; call
// SdSampleLoadCommit() once the load completes to copy stage -> RAM_D2. Other
// slots load straight into their (SDRAM) playback buffer.
int16_t* SdSampleLoadBuffer(uint8_t slot);
void     SdSampleLoadCommit(uint8_t slot, uint32_t frames);
int16_t* SdRecordBuffer();
int16_t* SdManageBuffer();
// Per-layer scratch holding the seam-baked copy of a loaded sample. The raw load
// stays in SdSampleBuffer(slot) for future re-bakes; the baked copy is what plays.
int16_t* SdBakedBuffer(uint8_t slot);
// Max loadable frames for a given playback slot (slot 0 = RAM_D2 cap, else SDRAM).
uint32_t SdSampleMaxFrames(uint8_t slot);
