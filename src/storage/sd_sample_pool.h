#pragma once

#include <cstdint>

#include "storage_limits.h"

static constexpr uint32_t kSdSampleMaxFrames = 480000; // 10 seconds @ 48kHz

int16_t* SdSampleBuffer(uint8_t slot);
uint32_t SdSampleMaxFrames();
