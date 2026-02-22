#pragma once

#include <cstdint>

static constexpr uint32_t kSdSampleMaxFrames = 240000; // 5 seconds @ 48kHz
static constexpr uint8_t kSdSampleSlots = 2;

int16_t* SdSampleBuffer(uint8_t slot);
uint32_t SdSampleMaxFrames();
