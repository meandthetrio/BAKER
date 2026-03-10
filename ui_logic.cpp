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
#include "sd_sample_pool.h"
#include <atomic>
#include <cmath>

using namespace daisy;

namespace
{
bool DispatchToParentPreview(AppState& app, Params& params, const UiInputEvent& e, uint32_t now_ms)
{
    if(!app.ui_parent_preview_active)
        return false;
    if(e.type != UiInputType::EncDelta || e.value == 0)
        return false;
    if(e.id != kUiEncPod && e.id != kUiEncExt)
        return false;

    UiScreenCtx parent_ctx{};
    parent_ctx.app = &app;
    parent_ctx.params = &params;
    parent_ctx.display = nullptr;
    parent_ctx.now_ms = now_ms;
    parent_ctx.shift = false;
    parent_ctx.lshift = false;
    parent_ctx.rshift = app.ui_rshift_held;

    UiInputEvent pe = e;
    if(e.id == kUiEncPod)
        pe.value = (e.value > 0) ? 1 : -1;

    if(app.ui_parent_preview_mode == 2
       && UiNav_Active(app.ui_nav) == UiScreenId::PerformProcess)
    {
        const bool saved_detail = app.perform_process_detail_active;
        app.perform_process_detail_active = false;
        const UiScreen& process = GetScreen(UiScreenId::PerformProcess);
        const bool handled = process.OnEvent && process.OnEvent(parent_ctx, pe);
        app.perform_process_detail_active = saved_detail;
        if(handled)
        {
            app.ui_dirty = true;
            return true;
        }
        return false;
    }

    if(app.ui_parent_preview_mode == 1 && app.ui_nav.top > 0)
    {
        const UiScreenId parent_id = app.ui_nav.stack[app.ui_nav.top - 1];
        const UiScreen& parent = GetScreen(parent_id);
        if(parent.OnEvent && parent.OnEvent(parent_ctx, pe))
        {
            app.ui_dirty = true;
            return true;
        }
    }
    return false;
}

void CommitParentPreviewSelection(AppState& app, Params& params, uint32_t now_ms)
{
    if(!app.ui_parent_preview_active)
        return;

    if(app.ui_parent_preview_mode == 2
       && UiNav_Active(app.ui_nav) == UiScreenId::PerformProcess)
    {
        const bool focus_has_submenu = (app.perform_process_main_cursor >= 2u);
        if(focus_has_submenu)
        {
            app.perform_process_detail_active = true;
        }
        else
        {
            app.perform_process_main_cursor = app.ui_parent_preview_origin_main_cursor;
            app.perform_process_fx_cursor = app.ui_parent_preview_origin_fx_cursor;
            app.perform_process_detail_active = app.ui_parent_preview_origin_process_detail;
        }
    }
    else if(app.ui_parent_preview_mode == 1
            && app.ui_nav.top > 0
            && app.ui_parent_preview_from_top > 0)
    {
        const uint8_t parent_top = static_cast<uint8_t>(app.ui_parent_preview_from_top - 1u);
        while(app.ui_nav.top > parent_top)
            UiNav_Pop(app.ui_nav);

        UiScreenCtx parent_ctx{};
        parent_ctx.app = &app;
        parent_ctx.params = &params;
        parent_ctx.display = nullptr;
        parent_ctx.now_ms = now_ms;
        parent_ctx.shift = false;
        parent_ctx.lshift = false;
        parent_ctx.rshift = app.ui_rshift_held;

        const UiScreen& parent = GetScreen(UiNav_Active(app.ui_nav));
        bool entered = false;
        if(parent.on_enter)
            entered = parent.on_enter(parent_ctx);
        if(!entered && app.ui_parent_preview_origin_screen != UiScreenId::COUNT)
            UiNav_Push(app.ui_nav, app.ui_parent_preview_origin_screen);
    }

    app.ui_parent_preview_active = false;
    app.ui_parent_preview_from_top = 0;
    app.ui_parent_preview_mode = 0;
    app.ui_parent_preview_origin_screen = UiScreenId::COUNT;
    app.ui_parent_preview_origin_main_cursor = 0;
    app.ui_parent_preview_origin_fx_cursor = 0;
    app.ui_parent_preview_origin_process_detail = false;
    app.ui_dirty = true;
}
} // namespace

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
    bool shift_held = app.ui_lshift_held;
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
            {
                app.ui_lshift_held = true;
                const UiScreenId active = UiNav_Active(app.ui_nav);
                if(active == UiScreenId::PerformProcess && app.perform_process_detail_active)
                {
                    app.ui_parent_preview_origin_screen = active;
                    app.ui_parent_preview_origin_main_cursor = app.perform_process_main_cursor;
                    app.ui_parent_preview_origin_fx_cursor = app.perform_process_fx_cursor;
                    app.ui_parent_preview_origin_process_detail = app.perform_process_detail_active;
                    app.ui_parent_preview_active = true;
                    app.ui_parent_preview_mode = 2;
                    app.ui_parent_preview_from_top = app.ui_nav.top;
                    app.ui_dirty = true;
                }
                else if(app.ui_nav.top > 0)
                {
                    app.ui_parent_preview_origin_screen = active;
                    app.ui_parent_preview_origin_main_cursor = app.perform_process_main_cursor;
                    app.ui_parent_preview_origin_fx_cursor = app.perform_process_fx_cursor;
                    app.ui_parent_preview_origin_process_detail = app.perform_process_detail_active;
                    app.ui_parent_preview_active = true;
                    app.ui_parent_preview_mode = 1;
                    app.ui_parent_preview_from_top = app.ui_nav.top;
                    app.ui_dirty = true;
                }
            }
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
            {
                app.ui_lshift_held = false;
                if(app.ui_parent_preview_active)
                {
                    CommitParentPreviewSelection(app, params, now_ms);
                    continue;
                }
            }
            else if(e.id == kUiBtnRShift)
                app.ui_rshift_held = false;
            else if(e.id == kUiBtnPod1)
                app.ui_btn1_held = false;
            else if(e.id == kUiBtnPod2)
                app.ui_btn2_held = false;
        }

        shift_held = app.ui_lshift_held;

        // POD BUTTON1 opens/closes SHIFT menu (short press, global).
        // Button1 is NEVER "enter/load"; EXT encoder click is enter.
        if(!shift_held && e.type == UiInputType::BtnDown && e.id == kUiBtnPod1)
        {
            // Toggle SHIFT menu.
            const UiScreenId active = UiNav_Active(app.ui_nav);
            if(active == UiScreenId::PerformProcess && app.perform_process_detail_active)
            {
                // Let PROCESS detail consume POD1 as "back to PROCESS".
                // Do not globally open SHIFT menu from inside FX detail.
            }
            else
            {
            if(active == UiScreenId::ShiftMenu)
            {
                UiNav_Pop(app.ui_nav);
            }
            else
            {
                // Reset SHIFT menu state on entry.
                app.shift_menu_cursor = 0;
                app.shift_menu_edit_volume = false;
                // Cancel any pending SD delete mode when opening SHIFT.
                app.sd_delete_mode = false;
                UiNav_Push(app.ui_nav, UiScreenId::ShiftMenu);
            }
            app.ui_dirty = true;
            continue;
            }
        }

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

        if(DispatchToParentPreview(app, params, e, now_ms))
            continue;

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
            shift_held = app.ui_lshift_held;
            ctx.shift = shift_held;
            ctx.lshift = app.ui_lshift_held;
            ctx.rshift = app.ui_rshift_held;
            s.OnEnter(ctx);
        }
        app.ui_dirty = true;
    }

    if(app.record_state == RecordUiState::Recording
       && app.rec_active.load(std::memory_order_acquire) == 0)
    {
        app.rec_monitor_enable.store(0, std::memory_order_release);
        const uint32_t rec_len = app.rec_length.load(std::memory_order_acquire);
        if(rec_len > 0)
        {
            uint8_t slot = app.rec_slot_pending.load(std::memory_order_acquire) & 1u;
            Sample& s = app.sd_slots[slot];
            s.pcm = SdSampleBuffer(slot);
            s.length = rec_len;
            s.sample_rate = 48000;
            s.root_key = 60;
            s.loop_start = 0;
            s.loop_end = rec_len;
            s.loop_enabled = false;

            SampleEdit edit = SampleEdit_Default(rec_len);
            app.sd_edit_slots[slot] = edit;
            app.sd_edit_pending = edit;
            app.sd_edit_slot.store(slot, std::memory_order_release);
            app.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
            app.sd_edit_ready.store(1, std::memory_order_release);

            app.sd_current_slot.store(slot, std::memory_order_release);
            app.record_slot = slot;
            app.record_state = RecordUiState::Review;
            app.ui_dirty = true;
        }
        else
        {
            app.record_state = RecordUiState::SourceSelect;
            app.ui_dirty = true;
        }
    }

    const bool record_review_active = (active_screen == UiScreenId::Record
                                       && app.record_state == RecordUiState::Review);
    if(record_review_active && app.record_preview_hold && !app.record_preview_gate)
    {
        const uint8_t slot = app.record_slot & 1u;
        const Sample& s = app.sd_slots[slot];
        if(s.pcm != nullptr && s.length > 0)
        {
            const uint8_t velocity = 120;
            const uint8_t vel_layer = Velocity_SelectLayer(velocity);
            Event evt = Event::NoteOnEvent(60, velocity);
            evt.value = static_cast<uint32_t>(slot) | (static_cast<uint32_t>(vel_layer) << 8);
            if(evtq.Push(evt))
            {
                app.events_pushed.fetch_add(1, std::memory_order_relaxed);
                app.record_preview_gate = true;
            }
            else
            {
                app.queue_overflows.fetch_add(1, std::memory_order_relaxed);
                app.ui_dirty = true;
            }
        }
    }
    if((!record_review_active || !app.record_preview_hold) && app.record_preview_gate)
    {
        const Event evt = Event::NoteOffEvent(60);
        if(evtq.Push(evt))
            app.events_pushed.fetch_add(1, std::memory_order_relaxed);
        else
            app.queue_overflows.fetch_add(1, std::memory_order_relaxed);
        app.record_preview_gate = false;
    }

    shift_held = app.ui_lshift_held;
    UiOverlay_Update(app.overlay, now_ms, false, app.value_edit.active);

    app.ui_in_ovf = UiInput_Dropped(app.ui_in);
    app.ui_in_hi = UiInput_HighWater(app.ui_in);

    if(app.engine_header_invert_until_ms != 0u
       && static_cast<int32_t>(now_ms - app.engine_header_invert_until_ms) >= 0)
    {
        app.engine_header_invert_until_ms = 0u;
        app.ui_dirty = true;
    }

    if(processed > 0 && !app.ui_dirty)
        app.ui_dirty = true;

    if(input_detected)
        app.last_input_ms = now_ms;

    // Keep animated Record screens responsive at UI tick rate.
    if(active_screen == UiScreenId::Record)
    {
        if(app.record_state == RecordUiState::Armed
           || app.record_state == RecordUiState::Countdown
           || app.record_state == RecordUiState::Recording)
        {
            app.ui_dirty = true;
        }
    }

    uint16_t worker_budget_us = 1500;
    if(app.ui_req_busy && app.ui_req_active == UiReqType::SaveRenderedWavCurrent)
        worker_budget_us = 6000;
    UiWorker_Tick(app, now_ms, worker_budget_us);
}
