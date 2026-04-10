#pragma once

#include <cstdint>

struct Sample
{
    const int16_t* pcm;         // 16-bit signed mono
    uint32_t       length;      // number of samples
    uint32_t       sample_rate; // Hz (expected 48000)
    uint8_t        root_key;    // MIDI note that plays at ratio=1.0
    uint32_t       loop_start;  // sample index
    uint32_t       loop_end;    // sample index (exclusive)
    bool           loop_enabled;
};
