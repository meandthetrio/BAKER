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
void ClearParentPreviewState(AppUiState& ui)
{
    ui.ui_parent_preview_active = false;
    ui.ui_parent_preview_from_top = 0;
    ui.ui_parent_preview_mode = 0;
    ui.ui_parent_preview_origin_screen = UiScreenId::COUNT;
    ui.ui_parent_preview_origin_main_cursor = 0;
    ui.ui_parent_preview_origin_fx_cursor = 0;
    ui.ui_parent_preview_origin_process_detail = false;
    ui.ui_parent_preview_origin_process_eq_graph = false;
}

bool PushPreviewNoteOn(EventQueueSPSC& evtq,
                       AppDiagnosticsState& diag,
                       AppUiState& ui,
                       uint8_t note,
                       uint8_t velocity,
                       uint8_t slot)
{
    const uint8_t vel_layer = Velocity_SelectLayer(velocity);
    Event evt = Event::NoteOnEvent(note, velocity);
    evt.value = static_cast<uint32_t>(slot) | (static_cast<uint32_t>(vel_layer) << 8);
    if(evtq.Push(evt))
    {
        diag.events_pushed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    diag.queue_overflows.fetch_add(1, std::memory_order_relaxed);
    ui.ui_dirty = true;
    return false;
}

bool PushPreviewNoteOff(EventQueueSPSC& evtq,
                        AppDiagnosticsState& diag,
                        AppUiState& ui,
                        uint8_t note)
{
    const Event evt = Event::NoteOffEvent(note);
    if(evtq.Push(evt))
    {
        diag.events_pushed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    diag.queue_overflows.fetch_add(1, std::memory_order_relaxed);
    ui.ui_dirty = true;
    return false;
}

void CloseDiagnosticsOverlay(UiOverlayState& overlay)
{
    overlay.modal_active = false;
    overlay.visible      = false;
}

bool DispatchToParentPreview(AppState& app, Params& params, const UiInputEvent& e, uint32_t now_ms)
{
    AppUiState& ui = app.ui;
    AppEngineState& engine = app.engine;
    if(!ui.ui_parent_preview_active || !ui.ui_lshift_held)
        return false;
    if(e.type != UiInputType::EncDelta || e.value == 0)
        return false;
    if(e.id != kUiEncPod && e.id != kUiEncExt)
        return false;

    UiScreenCtx parent_ctx{};
    UiSessionState parent_session{&app.ui, &app.engine, &app.recording, &app.project};
    UiScreenCtx_BindSession(parent_ctx, parent_session);
    parent_ctx.diag = &app.diag;
    parent_ctx.shared = &app.shared;
    parent_ctx.worker = &app.worker;
    parent_ctx.params = &params;
    parent_ctx.display = nullptr;
    parent_ctx.now_ms = now_ms;
    parent_ctx.shift = false;
    parent_ctx.lshift = false;
    parent_ctx.rshift = ui.ui_rshift_held;

    UiInputEvent pe = e;
    if(e.id == kUiEncPod)
        pe.value = (e.value > 0) ? 1 : -1;

    if(ui.ui_parent_preview_mode == 2
       && UiNav_Active(ui.ui_nav) == UiScreenId::PerformProcess)
    {
        const bool saved_detail = engine.process.perform_process_detail_active;
        const bool saved_eqg    = engine.process.perform_process_eq_graph_active;
        engine.process.perform_process_detail_active  = false;
        engine.process.perform_process_eq_graph_active = false;
        const UiScreen& process = GetScreen(UiScreenId::PerformProcess);
        const bool handled = process.OnEvent && process.OnEvent(parent_ctx, pe);
        engine.process.perform_process_detail_active   = saved_detail;
        engine.process.perform_process_eq_graph_active = saved_eqg;
        if(handled)
        {
            ui.ui_dirty = true;
            return true;
        }
        return false;
    }

    if(ui.ui_parent_preview_mode == 1 && ui.ui_nav.top > 0)
    {
        const UiScreenId parent_id = ui.ui_nav.stack[ui.ui_nav.top - 1];
        const UiScreen& parent = GetScreen(parent_id);
        if(parent.OnEvent && parent.OnEvent(parent_ctx, pe))
        {
            ui.ui_dirty = true;
            return true;
        }
    }
    return false;
}

void CommitParentPreviewSelection(AppState& app, Params& params, uint32_t now_ms)
{
    AppUiState& ui = app.ui;
    AppEngineState& engine = app.engine;
    if(!ui.ui_parent_preview_active)
        return;

    if(ui.ui_parent_preview_mode == 2
       && UiNav_Active(ui.ui_nav) == UiScreenId::PerformProcess)
    {
        const bool focus_has_submenu = (engine.process.perform_process_main_cursor >= 2u);
        if(focus_has_submenu)
        {
            engine.process.perform_process_detail_active   = ui.ui_parent_preview_origin_process_detail;
            engine.process.perform_process_eq_graph_active = ui.ui_parent_preview_origin_process_eq_graph;
        }
        else
        {
            engine.process.perform_process_main_cursor = ui.ui_parent_preview_origin_main_cursor;
            engine.process.perform_process_fx_cursor = ui.ui_parent_preview_origin_fx_cursor;
            engine.process.perform_process_detail_active   = ui.ui_parent_preview_origin_process_detail;
            engine.process.perform_process_eq_graph_active = ui.ui_parent_preview_origin_process_eq_graph;
        }
    }
    else if(ui.ui_parent_preview_mode == 1
            && ui.ui_nav.top > 0
            && ui.ui_parent_preview_from_top > 0)
    {
        const uint8_t parent_top = static_cast<uint8_t>(ui.ui_parent_preview_from_top - 1u);
        while(ui.ui_nav.top > parent_top)
            UiNav_Pop(ui.ui_nav);

        UiScreenCtx parent_ctx{};
        UiSessionState parent_session{&app.ui, &app.engine, &app.recording, &app.project};
        UiScreenCtx_BindSession(parent_ctx, parent_session);
        parent_ctx.diag = &app.diag;
        parent_ctx.shared = &app.shared;
        parent_ctx.worker = &app.worker;
        parent_ctx.params = &params;
        parent_ctx.display = nullptr;
        parent_ctx.now_ms = now_ms;
        parent_ctx.shift = false;
        parent_ctx.lshift = false;
        parent_ctx.rshift = ui.ui_rshift_held;

        const UiScreen& parent = GetScreen(UiNav_Active(ui.ui_nav));
        bool entered = false;
        if(parent.on_enter)
            entered = parent.on_enter(parent_ctx);
        if(!entered && ui.ui_parent_preview_origin_screen != UiScreenId::COUNT)
            UiNav_Push(ui.ui_nav, ui.ui_parent_preview_origin_screen);
    }

    ClearParentPreviewState(ui);
    ui.ui_dirty = true;
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
    AppSharedState& shared = app.shared;
    AppUiState& ui = app.ui;
    AppDiagnosticsState& diag = app.diag;
    const uint32_t now_ms = System::GetNow();
    Controls_Tick(controls_, ui, now_ms);

    if(shared.performance.sequencer.seq_last_ms == 0)
        shared.performance.sequencer.seq_last_ms = now_ms;
    uint32_t seq_dt = now_ms - shared.performance.sequencer.seq_last_ms;
    shared.performance.sequencer.seq_last_ms = now_ms;
    if(shared.performance.sequencer.seq_running)
    {
        shared.performance.sequencer.seq_accum_ms += seq_dt;
        float step_ms_f = 15000.0f / (float)shared.performance.sequencer.seq_bpm;
        if(step_ms_f < 1.0f)
            step_ms_f = 1.0f;
        const uint32_t step_ms = static_cast<uint32_t>(step_ms_f + 0.5f);
        while(shared.performance.sequencer.seq_accum_ms >= step_ms)
        {
            shared.performance.sequencer.seq_accum_ms -= step_ms;
            shared.performance.plocks.plock_pattern.step_index
                = (shared.performance.plocks.plock_pattern.step_index + 1) % kSteps;
            if(shared.performance.plocks.plock_apply_enabled)
                PLocks_PublishCurrentStep(shared.performance.plocks.plocks,
                                          shared.performance.plocks.plock_pattern);
            ui.ui_dirty = true;
        }
    }

    // 1-second rolling peak of active voices (sampled at control rate).
    const uint32_t active_now = diag.voices_active.load(std::memory_order_relaxed);
    if(peak_window_start_ms_ == 0)
    {
        peak_window_start_ms_ = now_ms;
        peak_active_          = active_now;
        diag.voices_peak_1s.store(peak_active_, std::memory_order_relaxed);
    }
    else
    {
        if(active_now > peak_active_)
            peak_active_ = active_now;

        // Keep OLED updated quickly for short bursts (e.g. stress test).
        diag.voices_peak_1s.store(peak_active_, std::memory_order_relaxed);

        if((now_ms - peak_window_start_ms_) >= 1000)
        {
            diag.voices_peak_1s.store(peak_active_, std::memory_order_relaxed);
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
            diag.events_pushed.fetch_add(1, std::memory_order_relaxed);
            slot.active = false;
        }
        else
        {
            diag.queue_overflows.fetch_add(1, std::memory_order_relaxed);
            ui.ui_dirty = true;
        }
    }

}

void UILogic::UiTick(AppState& app, Params& params, EventQueueSPSC& evtq, uint32_t now_ms)
{
    static constexpr uint32_t kUiTickMs = 16;
    static constexpr int kMaxUiEventsPerTick = 32;

    AppUiState& ui = app.ui;
    AppEngineState& engine = app.engine;
    AppRecordingState& recording = app.recording;
    AppDiagnosticsState& diag = app.diag;
    AppSharedState& shared = app.shared;
    AppWorkerState& worker = app.worker;

    if((now_ms - last_ui_tick_ms_) < kUiTickMs)
        return;
    last_ui_tick_ms_ = now_ms;

    bool input_detected = false;

    UiScreenCtx ctx{};
    UiSessionState ctx_session{&app.ui, &app.engine, &app.recording, &app.project};
    UiScreenCtx_BindSession(ctx, ctx_session);
    ctx.diag = &app.diag;
    ctx.shared = &app.shared;
    ctx.worker = &app.worker;
    ctx.params = &params;
    ctx.display = nullptr;
    ctx.now_ms = now_ms;
    bool shift_held = ui.ui_lshift_held;
    ctx.shift = shift_held;
    ctx.lshift = ui.ui_lshift_held;
    ctx.rshift = ui.ui_rshift_held;

    UiInputEvent e{};
    int processed = 0;
    while(processed < kMaxUiEventsPerTick && UiInput_Pop(ui.ui_in, e))
    {
        processed++;
        ui.ui_in_pop++;
        input_detected = true;
        const bool blank_screen_active = ui.ui_blank_screen_active;
        const bool rename_project_active = (UiNav_Active(ui.ui_nav) == UiScreenId::RenameProject);

        if(rename_project_active
           && (e.type == UiInputType::BtnDown
               || e.type == UiInputType::BtnUp
               || e.type == UiInputType::BtnLong))
        {
            if(e.id == kUiBtnLShift)
            {
                const bool had_preview = ui.ui_parent_preview_active;
                ui.ui_lshift_held = false;
                ClearParentPreviewState(ui);
                if(had_preview)
                    ui.ui_dirty = true;
                continue;
            }

            if(e.id == kUiBtnPod1)
            {
                ui.ui_btn1_held = false;
                continue;
            }

            if(e.id == kUiBtnPod2)
            {
                ui.ui_btn2_held = false;
                continue;
            }
        }

        if(e.type == UiInputType::BtnDown)
        {
            if(e.id == kUiBtnLShift)
            {
                ui.ui_lshift_held = true;
                if(!blank_screen_active)
                {
                    const UiScreenId active = UiNav_Active(ui.ui_nav);
                    if(active == UiScreenId::PerformProcess
                       && (engine.process.perform_process_detail_active || engine.process.perform_process_eq_graph_active))
                    {
                        ui.ui_parent_preview_origin_screen = active;
                        ui.ui_parent_preview_origin_main_cursor = engine.process.perform_process_main_cursor;
                        ui.ui_parent_preview_origin_fx_cursor = engine.process.perform_process_fx_cursor;
                        ui.ui_parent_preview_origin_process_detail = engine.process.perform_process_detail_active;
                        ui.ui_parent_preview_origin_process_eq_graph = engine.process.perform_process_eq_graph_active;
                        ui.ui_parent_preview_active = true;
                        ui.ui_parent_preview_mode = 2;
                        ui.ui_parent_preview_from_top = ui.ui_nav.top;
                        ui.ui_dirty = true;
                    }
                    else if(ui.ui_nav.top > 0)
                    {
                        ui.ui_parent_preview_origin_screen = active;
                        ui.ui_parent_preview_origin_main_cursor = engine.process.perform_process_main_cursor;
                        ui.ui_parent_preview_origin_fx_cursor = engine.process.perform_process_fx_cursor;
                        ui.ui_parent_preview_origin_process_detail = engine.process.perform_process_detail_active;
                        ui.ui_parent_preview_origin_process_eq_graph = engine.process.perform_process_eq_graph_active;
                        ui.ui_parent_preview_active = true;
                        ui.ui_parent_preview_mode = 1;
                        ui.ui_parent_preview_from_top = ui.ui_nav.top;
                        ui.ui_dirty = true;
                    }
                }
            }
            else if(e.id == kUiBtnRShift)
                ui.ui_rshift_held = true;
            else if(e.id == kUiBtnPod1)
                ui.ui_btn1_held = true;
            else if(e.id == kUiBtnPod2)
                ui.ui_btn2_held = true;
        }
        else if(e.type == UiInputType::BtnUp)
        {
            if(e.id == kUiBtnLShift)
            {
                ui.ui_lshift_held = false;
                if(!blank_screen_active && ui.ui_parent_preview_active)
                {
                    CommitParentPreviewSelection(app, params, now_ms);
                    continue;
                }
            }
            else if(e.id == kUiBtnRShift)
                ui.ui_rshift_held = false;
            else if(e.id == kUiBtnPod1)
                ui.ui_btn1_held = false;
            else if(e.id == kUiBtnPod2)
                ui.ui_btn2_held = false;
        }

        shift_held = ui.ui_lshift_held;
        const bool both_shifts_held = ui.ui_lshift_held && ui.ui_rshift_held;

        if(ui.ui_blank_screen_active)
        {
            if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
            {
                ClearParentPreviewState(ui);
                ui.ui_blank_screen_active = false;
                ui.ui_dirty = true;
            }
            continue;
        }

        if(e.type == UiInputType::BtnDown
           && ui.ui_lshift_held
           && e.id == kUiBtnPod1)
        {
            if(!diag.overlay.modal_active)
            {
                diag.overlay.modal_active = true;
                diag.overlay.page = kDiagOverlayPageSys;
                UiOverlay_Update(diag.overlay, now_ms);
                ClearParentPreviewState(ui);
                ui.ui_dirty = true;
            }
            continue;
        }

        if(diag.overlay.modal_active)
        {
            if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
            {
                diag.overlay.page = static_cast<uint8_t>((diag.overlay.page + 1u)
                                                         % kDiagOverlayPageCount);
                ui.ui_dirty = true;
                continue;
            }
            if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
            {
                CloseDiagnosticsOverlay(diag.overlay);
                ui.ui_dirty = true;
            }
            continue;
        }

        // POD BUTTON1 opens/closes SHIFT menu (short press, global).
        // Button1 is NEVER "enter/load"; EXT encoder click is enter.
        if(!shift_held && e.type == UiInputType::BtnDown && e.id == kUiBtnPod1)
        {
            // Toggle SHIFT menu.
            const UiScreenId active = UiNav_Active(ui.ui_nav);
            if(active == UiScreenId::PerformProcess
               && (engine.process.perform_process_detail_active || engine.process.perform_process_eq_graph_active))
            {
                // Let PROCESS detail / EQ graph consume POD1 as "back to PROCESS".
                // Do not globally open SHIFT menu from inside FX detail.
            }
            else
            {
            if(active == UiScreenId::ShiftMenu)
            {
                ui.shift_menu_bootloader_armed = false;
                ui.shift_menu_bootloader_arm_start_ms = 0;
                ui.shift_menu_bootloader_loading = false;
                ui.shift_menu_bootloader_loading_start_ms = 0;
                ui.shift_menu_firmware_update_active = false;
                UiNav_Pop(ui.ui_nav);
            }
            else
            {
                // Reset SHIFT menu state on entry.
                ui.shift_menu_cursor = 0;
                ui.shift_menu_edit_volume = false;
                ui.shift_menu_bootloader_armed = false;
                ui.shift_menu_bootloader_arm_start_ms = 0;
                ui.shift_menu_bootloader_loading = false;
                ui.shift_menu_bootloader_loading_start_ms = 0;
                ui.shift_menu_firmware_update_active = false;
                // Cancel any pending SD delete mode when opening SHIFT.
                ui.sd_delete_mode = false;
                UiNav_Push(ui.ui_nav, UiScreenId::ShiftMenu);
            }
            ui.ui_dirty = true;
            continue;
            }
        }

        if(e.type == UiInputType::BtnDown
           && both_shifts_held
           && e.id == kUiBtnPod2)
        {
            ClearParentPreviewState(ui);
            ui.ui_blank_screen_active = true;
            ui.ui_dirty = true;
            continue;
        }

        if(!shift_held && e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
        {
            if(ui.value_edit.active)
            {
                UiValueEdit_Cancel(ui.value_edit);
                ui.ui_dirty = true;
                continue;
            }
        }

        if(shift_held && e.type == UiInputType::BtnDown)
        {
            if(e.id == kUiBtnPodEnc)
            {
                shared.performance.plocks.plock_apply_enabled = !shared.performance.plocks.plock_apply_enabled;
                ui.ui_dirty = true;
                continue;
            }
            if(e.id == kUiBtnExtEnc)
            {
                const uint8_t cur = shared.performance.modulation.lfo_wave.load(std::memory_order_relaxed);
                const uint8_t next = (cur == 0) ? 1u : 0u;
                shared.performance.modulation.lfo_wave.store(next, std::memory_order_relaxed);
                ui.ui_dirty = true;
                continue;
            }
            if(e.id == kUiBtnPod2)
            {
                const Event evt = Event::AllNotesOffEvent();
                if(evtq.Push(evt))
                    diag.events_pushed.fetch_add(1, std::memory_order_relaxed);
                else
                {
                    diag.queue_overflows.fetch_add(1, std::memory_order_relaxed);
                    ui.ui_dirty = true;
                }

                for(size_t i = 0; i < kMaxPendingNoteOffs; i++)
                    pending_note_offs_[i].active = false;
                ui.ui_dirty = true;
                continue;
            }
        }

        if(DispatchToParentPreview(app, params, e, now_ms))
            continue;

        ctx.shift = shift_held;
        ctx.lshift = ui.ui_lshift_held;
        ctx.rshift = ui.ui_rshift_held;
        UiRouter_DispatchEvent(ctx, e);
    }

    const UiScreenId active_screen = UiNav_Active(ui.ui_nav);
    if(active_screen != ui.ui_active_screen)
    {
        ui.ui_active_screen = active_screen;
        const UiScreen& s = GetScreen(active_screen);
        if(s.OnEnter)
        {
            shift_held = ui.ui_lshift_held;
            ctx.shift = shift_held;
            ctx.lshift = ui.ui_lshift_held;
            ctx.rshift = ui.ui_rshift_held;
            s.OnEnter(ctx);
        }
        ui.ui_dirty = true;
    }

    if(recording.record_state == RecordUiState::Recording
       && shared.recording.rec_active.load(std::memory_order_acquire) == 0)
    {
        shared.recording.rec_monitor_enable.store(0, std::memory_order_release);
        const uint32_t rec_len = shared.recording.rec_length.load(std::memory_order_acquire);
        if(rec_len > 0)
        {
            uint8_t slot = shared.recording.rec_slot_pending.load(std::memory_order_acquire) & 1u;
            Sample& s = shared.sample.publish.sd_slots[slot];
            s.pcm = SdSampleBuffer(slot);
            s.length = rec_len;
            s.sample_rate = 48000;
            s.root_key = 60;
            s.loop_start = 0;
            s.loop_end = rec_len;
            s.loop_enabled = false;

            SampleEdit edit = SampleEdit_Default(rec_len);
            shared.sample.edit.sd_edit_slots[slot] = edit;
            shared.sample.edit.sd_edit_pending = edit;
            shared.sample.edit.sd_edit_slot.store(slot, std::memory_order_release);
            shared.sample.edit.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
            shared.sample.edit.sd_edit_ready.store(1, std::memory_order_release);

            shared.sample.publish.sd_current_slot.store(slot, std::memory_order_release);
            recording.record_slot = slot;
            recording.record_state = RecordUiState::Review;
            ui.ui_dirty = true;
        }
        else
        {
            recording.record_state = RecordUiState::SourceSelect;
            ui.ui_dirty = true;
        }
    }

    const bool record_review_active = (active_screen == UiScreenId::Record
                                       && recording.record_state == RecordUiState::Review);
    if(record_review_active && recording.record_preview_hold && !recording.record_preview_gate)
    {
        const uint8_t slot = recording.record_slot & 1u;
        const Sample& s = shared.sample.publish.sd_slots[slot];
        if(s.pcm != nullptr && s.length > 0)
        {
            if(PushPreviewNoteOn(evtq, diag, ui, 60, 120, slot))
                recording.record_preview_gate = true;
        }
    }
    if((!record_review_active || !recording.record_preview_hold) && recording.record_preview_gate)
    {
        if(PushPreviewNoteOff(evtq, diag, ui, 60))
            recording.record_preview_gate = false;
    }

    const bool trim_preview_active = (active_screen == UiScreenId::PerformWaveEdit);
    if(trim_preview_active && ui.ui_trim_preview_hold && !ui.ui_trim_preview_gate)
    {
        const uint8_t slot = engine.perform_nav.perform_layer & 1u;
        const Sample& s = shared.sample.publish.sd_slots[slot];
        if(s.pcm != nullptr && s.length > 0)
        {
            if(PushPreviewNoteOn(evtq, diag, ui, 60, 120, slot))
                ui.ui_trim_preview_gate = true;
        }
    }
    if((!trim_preview_active || !ui.ui_trim_preview_hold) && ui.ui_trim_preview_gate)
    {
        if(PushPreviewNoteOff(evtq, diag, ui, 60))
            ui.ui_trim_preview_gate = false;
    }

    shift_held = ui.ui_lshift_held;
    UiOverlay_Update(diag.overlay, now_ms);

    ui.ui_in_ovf = UiInput_Dropped(ui.ui_in);
    ui.ui_in_hi = UiInput_HighWater(ui.ui_in);

    if(engine.layer.engine_header_invert_until_ms != 0u
       && static_cast<int32_t>(now_ms - engine.layer.engine_header_invert_until_ms) >= 0)
    {
        engine.layer.engine_header_invert_until_ms = 0u;
        ui.ui_dirty = true;
    }

    if(processed > 0 && !ui.ui_dirty)
        ui.ui_dirty = true;

    if(input_detected)
        ui.last_input_ms = now_ms;

    // Keep animated Record screens responsive at UI tick rate.
    if(active_screen == UiScreenId::Record)
    {
        if(recording.record_state == RecordUiState::Armed
           || recording.record_state == RecordUiState::Countdown
           || recording.record_state == RecordUiState::Recording)
        {
            ui.ui_dirty = true;
        }
    }

    uint16_t worker_budget_us = 1500;
    if(worker.ui_req_busy
       && worker.ui_req_active == UiReqType::SaveRenderedWavCurrent)
        worker_budget_us = 6000;
    UiWorker_Tick(app.ui,
                  app.project,
                  app.engine,
                  app.shared,
                  app.worker,
                  params,
                  now_ms,
                  worker_budget_us);

    if(ui.save_project_pending
       && worker.ui_req_done_count != ui.save_project_pending_done_count)
    {
        ui.save_project_pending = false;
        ui.project_rename_for_new_save = false;
        ui.project_rename_new_save_slot = 0;
        ui.pending_named_save_active = false;
        ui.pending_named_save_slot = 0;
        ui.pending_named_save_name[0] = '\0';
        if(worker.ui_req_result < 0)
        {
            if(UiNav_Active(ui.ui_nav) != UiScreenId::ProjectStatus)
                UiNav_Push(ui.ui_nav, UiScreenId::ProjectStatus);
        }
        ui.ui_dirty = true;
    }

    if(ui.project_style_update_pending
       && worker.ui_req_done_count != ui.project_style_update_pending_done_count)
    {
        ui.project_style_update_pending = false;
        if(worker.ui_req_result < 0)
        {
            if(UiNav_Active(ui.ui_nav) != UiScreenId::ProjectStatus)
                UiNav_Push(ui.ui_nav, UiScreenId::ProjectStatus);
        }
        ui.ui_dirty = true;
    }
}
