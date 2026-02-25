#include "ui_logic.h"
#include "keygroups.h"
#include "velocity_layers.h"
#include "mod_matrix.h"
#include "plocks.h"
#include "macros.h"
#include "ui_screens.h"
#include "ui_value_edit.h"
#include "ui_overlay.h"
#include "ui_worker.h"
#include <atomic>
#include <cmath>

using namespace daisy;

void UILogic::Init(DaisyPod& hw)
{
    controls_.hw = &hw;
    Controls_Init(controls_);
}

void UILogic::ControlTick(DaisyPod& hw, AppState& app, Params& params, EventQueueSPSC& evtq)
{
    (void)hw;
    (void)params;
    const uint32_t now_ms = System::GetNow();
    Controls_Tick(controls_, app, now_ms);

    if(app.seq_last_ms == 0)
        app.seq_last_ms = now_ms;
    uint32_t seq_dt = now_ms - app.seq_last_ms;
    app.seq_last_ms = now_ms;
    if(app.seq_running)
    {
        app.seq_accum_ms += seq_dt;
        float step_ms_f = 15000.0f / (float)app.seq_bpm;
        if(step_ms_f < 1.0f)
            step_ms_f = 1.0f;
        const uint32_t step_ms = static_cast<uint32_t>(step_ms_f + 0.5f);
        while(app.seq_accum_ms >= step_ms)
        {
            app.seq_accum_ms -= step_ms;
            app.plock_pattern.step_index = (app.plock_pattern.step_index + 1) % kSteps;
            if(app.plock_apply_enabled)
                PLocks_PublishCurrentStep(app.plocks, app.plock_pattern);
            app.ui_dirty = true;
        }
    }

    // 1-second rolling peak of active voices (sampled at control rate).
    const uint32_t active_now = app.voices_active.load(std::memory_order_relaxed);
    if(peak_window_start_ms_ == 0)
    {
        peak_window_start_ms_ = now_ms;
        peak_active_          = active_now;
        app.voices_peak_1s.store(peak_active_, std::memory_order_relaxed);
    }
    else
    {
        if(active_now > peak_active_)
            peak_active_ = active_now;

        // Keep OLED updated quickly for short bursts (e.g. stress test).
        app.voices_peak_1s.store(peak_active_, std::memory_order_relaxed);

        if((now_ms - peak_window_start_ms_) >= 1000)
        {
            app.voices_peak_1s.store(peak_active_, std::memory_order_relaxed);
            peak_window_start_ms_ = now_ms;
            peak_active_          = active_now;
        }
    }

    // Drain scheduled NoteOffs (no heap; fixed list)
    for(size_t i = 0; i < kMaxPendingNoteOffs; i++)
    {
        auto& slot = pending_note_offs_[i];
        if(!slot.active)
            continue;
        if((int32_t)(now_ms - slot.due_ms) < 0)
            continue;

        const Event evt = Event::NoteOffEvent(slot.note);
        if(evtq.Push(evt))
        {
            app.events_pushed.fetch_add(1, std::memory_order_relaxed);
            slot.active = false;
        }
        else
        {
            app.queue_overflows.fetch_add(1, std::memory_order_relaxed);
            app.ui_dirty = true;
        }
    }

}

void UILogic::UiTick(AppState& app, Params& params, EventQueueSPSC& evtq, uint32_t now_ms)
{
    static constexpr uint32_t kUiTickMs = 16;
    static constexpr int kMaxUiEventsPerTick = 32;

    if((now_ms - last_ui_tick_ms_) < kUiTickMs)
        return;
    last_ui_tick_ms_ = now_ms;

    bool input_detected = false;

    UiScreenCtx ctx{};
    ctx.app = &app;
    ctx.params = &params;
    ctx.display = nullptr;
    ctx.now_ms = now_ms;
    bool shift_held = app.ui_lshift_held || app.ui_rshift_held;
    ctx.shift = shift_held;
    ctx.lshift = app.ui_lshift_held;
    ctx.rshift = app.ui_rshift_held;

    UiInputEvent e{};
    int processed = 0;
    while(processed < kMaxUiEventsPerTick && UiInput_Pop(app.ui_in, e))
    {
        processed++;
        app.ui_in_pop++;
        input_detected = true;

        if(e.type == UiInputType::BtnDown)
        {
            if(e.id == kUiBtnLShift)
                app.ui_lshift_held = true;
            else if(e.id == kUiBtnRShift)
                app.ui_rshift_held = true;
            else if(e.id == kUiBtnPod1)
                app.ui_btn1_held = true;
            else if(e.id == kUiBtnPod2)
                app.ui_btn2_held = true;
        }
        else if(e.type == UiInputType::BtnUp)
        {
            if(e.id == kUiBtnLShift)
                app.ui_lshift_held = false;
            else if(e.id == kUiBtnRShift)
                app.ui_rshift_held = false;
            else if(e.id == kUiBtnPod1)
                app.ui_btn1_held = false;
            else if(e.id == kUiBtnPod2)
                app.ui_btn2_held = false;
        }

        shift_held = app.ui_lshift_held || app.ui_rshift_held;
        if(!shift_held && e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
        {
            if(app.value_edit.active)
            {
                UiValueEdit_Cancel(app.value_edit);
                app.ui_dirty = true;
                continue;
            }
        }

        if(shift_held && e.type == UiInputType::BtnDown)
        {
            if(e.id == kUiBtnPod1)
            {
                app.seq_running = !app.seq_running;
                app.ui_dirty = true;
                continue;
            }
            if(e.id == kUiBtnPodEnc)
            {
                app.plock_apply_enabled = !app.plock_apply_enabled;
                app.ui_dirty = true;
                continue;
            }
            if(e.id == kUiBtnExtEnc)
            {
                const uint8_t cur = app.lfo_wave.load(std::memory_order_relaxed);
                const uint8_t next = (cur == 0) ? 1u : 0u;
                app.lfo_wave.store(next, std::memory_order_relaxed);
                app.ui_dirty = true;
                continue;
            }
            if(e.id == kUiBtnPod2)
            {
                const Event evt = Event::AllNotesOffEvent();
                if(evtq.Push(evt))
                    app.events_pushed.fetch_add(1, std::memory_order_relaxed);
                else
                {
                    app.queue_overflows.fetch_add(1, std::memory_order_relaxed);
                    app.ui_dirty = true;
                }

                for(size_t i = 0; i < kMaxPendingNoteOffs; i++)
                    pending_note_offs_[i].active = false;
                app.ui_dirty = true;
                continue;
            }
        }

        ctx.shift = shift_held;
        ctx.lshift = app.ui_lshift_held;
        ctx.rshift = app.ui_rshift_held;
        UiRouter_DispatchEvent(ctx, e);
    }

    const UiScreenId active_screen = UiNav_Active(app.ui_nav);
    if(active_screen != app.ui_active_screen)
    {
        app.ui_active_screen = active_screen;
        const UiScreen& s = GetScreen(active_screen);
        if(s.OnEnter)
        {
            shift_held = app.ui_lshift_held || app.ui_rshift_held;
            ctx.shift = shift_held;
            ctx.lshift = app.ui_lshift_held;
            ctx.rshift = app.ui_rshift_held;
            s.OnEnter(ctx);
        }
        app.ui_dirty = true;
    }

    shift_held = app.ui_lshift_held || app.ui_rshift_held;
    UiOverlay_Update(app.overlay, now_ms, shift_held, app.value_edit.active);

    app.ui_in_ovf = UiInput_Dropped(app.ui_in);
    app.ui_in_hi = UiInput_HighWater(app.ui_in);

    if(processed > 0 && !app.ui_dirty)
        app.ui_dirty = true;

    if(input_detected)
        app.last_input_ms = now_ms;

    UiWorker_Tick(app, now_ms, 1500);
}
