#pragma once

#include <cstdint>

struct Keygroup
{
    uint8_t lo_note;
    uint8_t hi_note;
    uint8_t sample_index;
};

uint8_t Keygroups_SelectSampleIndex(uint8_t midi_note);
