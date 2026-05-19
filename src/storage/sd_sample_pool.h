#pragma once

#include <cstdint>

#include "storage_limits.h"

static constexpr uint32_t kSdSampleMaxFrames = 240000; // 5 seconds @ 48kHz

int16_t* SdSampleBuffer(uint8_t slot);
int16_t* SdRecordBuffer();
uint32_t SdSampleMaxFrames();
