#pragma once

#include <cstdint>

enum class RecordInputSource : uint8_t
{
    LineIn = 0,
    Mic = 1,
};

enum class RecordUiState : uint8_t
{
    SourceSelect = 0,
    Armed,
    Countdown,
    Recording,
    Review,
    TargetSelect,
    BackConfirm,
    SaveWait,
};

// Main-thread recording UI and lifecycle state.
struct AppRecordingState
{
    RecordUiState record_state = RecordUiState::SourceSelect;
    uint8_t record_source_index = 0;
    uint8_t record_target_index = 0;
    uint32_t record_countdown_start_ms = 0;
    double record_anim_start_ms = -1.0;
    uint8_t record_slot = 0;
    bool    record_preview_hold = false;
    bool    record_preview_gate = false;
    uint32_t record_preview_oneshot_until_ms = 0;
};
