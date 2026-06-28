#pragma once
#include "app_state.h"
#include "params.h"
#include "daisy_pod.h"
#include "event_queue.h"
#include "controls.h"

class UILogic
{
  public:
    void Init(daisy::DaisyPod& hw);
    void ControlTick(daisy::DaisyPod& hw, AppState& app, Params& params, EventQueueSPSC& evtq);
    // Hardware input scan (encoders/buttons debounce + event enqueue). Runs in the
    // 1 kHz input timer ISR (main.cpp) so input sampling is immune to main-loop
    // load — heavy CRAFT preview/render no longer stalls the encoders.
    void ScanInputsIsr(AppState& app);
    void UiTick(AppState& app, Params& params, EventQueueSPSC& evtq, uint32_t now_ms);

  private:
    struct PendingNoteOff
    {
        bool     active = false;
        uint8_t  note   = 0;
        uint8_t  _pad0  = 0;
        uint16_t _pad1  = 0;
        uint32_t due_ms = 0;
    };
    static constexpr size_t kMaxPendingNoteOffs = 16;

    ControlsState  controls_{};
    PendingNoteOff pending_note_offs_[kMaxPendingNoteOffs];
    uint32_t       peak_window_start_ms_ = 0;
    uint32_t       peak_active_          = 0;
    uint32_t       last_ui_tick_ms_      = 0;

    const float enc_step_ = 0.02f;

    static constexpr float kLpfMinHz = 80.0f;
    static constexpr float kLpfMaxHz = 12000.0f;

    bool SchedulePendingNoteOff(uint8_t note, uint32_t due_ms);
    void CancelPendingNoteOff(uint8_t note);
};
