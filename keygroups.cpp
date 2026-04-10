#include "keygroups.h"

#include <cstddef>

static constexpr Keygroup kKeygroups[] = {
    {0, 59, 0},
    {60, 127, 1},
};

uint8_t Keygroups_SelectSampleIndex(uint8_t midi_note)
{
    for(size_t i = 0; i < (sizeof(kKeygroups) / sizeof(kKeygroups[0])); ++i)
    {
        const Keygroup& kg = kKeygroups[i];
        if(midi_note >= kg.lo_note && midi_note <= kg.hi_note)
            return kg.sample_index;
    }
    return 0;
}
