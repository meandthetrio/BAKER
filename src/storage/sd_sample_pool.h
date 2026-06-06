#pragma once

#include <cstdint>

#include "storage_limits.h"

static constexpr uint32_t kSdSampleMaxFrames = 240000; // 5 seconds @ 48kHz

int16_t* SdSampleBuffer(uint8_t slot);
int16_t* SdRecordBuffer();
int16_t* SdManageBuffer();
// Per-layer scratch holding the seam-baked copy of a loaded sample. The raw load
// stays in SdSampleBuffer(slot) for future re-bakes; the baked copy is what plays.
int16_t* SdBakedBuffer(uint8_t slot);
uint32_t SdSampleMaxFrames();
