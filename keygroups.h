#pragma once

#include <cstdint>

static constexpr uint8_t kPerformKeyzoneMinNote = 21u; // A0
static constexpr uint8_t kPerformKeyzoneMaxNote = 108u; // C8
static constexpr uint8_t kPerformKeyzoneDefaultSplitAHi = 71u; // B4
static constexpr uint8_t kPerformKeyzoneDefaultSplitBLo = 72u; // C5

struct Keygroup
{
    uint8_t lo_note;
    uint8_t hi_note;
    uint8_t sample_index;
};

static inline bool KeyzoneContainsNote(uint8_t lo_note, uint8_t hi_note, uint8_t midi_note)
{
    return midi_note >= lo_note && midi_note <= hi_note;
}

uint8_t Keygroups_SelectSampleIndex(uint8_t midi_note);
