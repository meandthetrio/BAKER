#include "ui_logic.h"
#include "keygroups.h"
#include "velocity_layers.h"
#include "mod_matrix.h"
#include "plocks.h"
#include "macros.h"
#include "ui_screens.h"
#include "ui_screens_internal.h"
#include "ui_screen_record_internal.h"
#include "ui_value_edit.h"
#include "ui_overlay.h"
#include "ui_worker.h"
#include "sd_sample_pool.h"
#include "sample_bake.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace daisy;

namespace
{
static constexpr uint8_t kRenderPreviewVelocity = 120u;

void PrepareSharedRecordReview(AppUiState& ui,
                               AppRecordingState& recording,
                               AppSharedState& shared)
{
    ui.record_render_phase = RecordRenderPhase::Review;
    ui.record_render_review_focus = 0;
    ui.record_render_status[0] = '\0';
    ui.render_review_trim_entry = shared.recording.rec_edit;
    ui.render_review_trim_has_entry = false;
    ui.render_sample_rename_active = false;
    ui.render_sample_rename_wait_for_worker = false;
    ui.record_render_save_stem[0] = '\0';
    recording.record_state = RecordUiState::Armed;
    recording.record_anim_start_ms = -1.0;
    ui.record_menu_armed_back_returns_to_menu = false;
}

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

UiScreenId ReviewParentScreen(const AppUiState& ui)
{
    return (ui.ui_nav.top > 0u) ? ui.ui_nav.stack[ui.ui_nav.top - 1u]
                                : UiScreenId::COUNT;
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

bool PushAllNotesOff(EventQueueSPSC& evtq, AppDiagnosticsState& diag, AppUiState& ui)
{
    const Event evt = Event::AllNotesOffEvent();
    if(evtq.Push(evt))
    {
        diag.events_pushed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    diag.queue_overflows.fetch_add(1, std::memory_order_relaxed);
    ui.ui_dirty = true;
    return false;
}

uint8_t RenderPreviewMidiNote(const AppUiState& ui)
{
    const int note = 60 + static_cast<int>(ui.record_render_note_offset);
    if(note < 0)
        return 0u;
    if(note > 127)
        return 127u;
    return static_cast<uint8_t>(note);
}

bool RenderPreviewLayerEligible(const Params& params,
                                const AppSharedState& shared,
                                uint8_t layer,
                                uint8_t note)
{
    if(layer >= 2u)
        return false;

    const Sample& sample = shared.sample.publish.sd_slots[layer];
    if(sample.pcm == nullptr || sample.length == 0)
        return false;

    // Keyzone is modulation-only now; it no longer gates which notes sound.
    (void)params;
    (void)note;
    return true;
}

bool RenderPreviewAvailable(const Params& params,
                            const AppUiState& ui,
                            const AppSharedState& shared)
{
    const uint8_t note = RenderPreviewMidiNote(ui);
    for(uint8_t layer = 0; layer < 2u; ++layer)
    {
        if(RenderPreviewLayerEligible(params, shared, layer, note))
            return true;
    }
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

void FinalizeRenderCapture(AppUiState& ui,
                           AppRecordingState& recording,
                           AppSharedState& shared)
{
    const uint32_t rec_len = shared.recording.rec_length.load(std::memory_order_acquire);
    if(rec_len == 0u)
        return;

    Sample& s = shared.recording.rec_sample;
    s.pcm = SdRecordBuffer();
    s.length = rec_len;
    s.sample_rate = 48000;
    s.root_key = ui.record_render_note;
    s.loop_start = 0;
    s.loop_end = rec_len;
    s.loop_enabled = false;

    shared.recording.rec_edit = SampleEdit_Default(rec_len);
    recording.record_slot = kRecordPreviewSampleIndex;
}

void MaybeReturnToCraftAfterLoad(AppUiState& ui)
{
    if(!ui.craft_browser_open)
        return;
    if(!ui.craft_browser_wait_for_load)
        return;
    if(UiNav_Active(ui.ui_nav) != UiScreenId::SdBrowse)
        return;
    if(ui.sd.load_in_progress)
        return;
    if(std::strncmp(ui.sd.status, "LOADED", 6) != 0)
        return;

    std::snprintf(ui.craft_loaded_path,
                  sizeof(ui.craft_loaded_path),
                  "%s",
                  ui.sd.last_loaded_path);
    ExtractBaseName(ui.sd.last_loaded_path,
                    ui.craft_loaded_name,
                    sizeof(ui.craft_loaded_name));
    ui.craft_browser_open = false;
    ui.craft_browser_wait_for_load = false;
    // Render-then-play preview: a freshly loaded sample matches its own raw
    // audio, so the preview is clean (LED2 green) when no effect is selected.
    // If effects are already configured, the raw load doesn't reflect them yet
    // -> dirty (LED2 orange, needs a render). Plugin 0 == "----" (no effect).
    ui.craft_preview_dirty = (ui.craft_slot_plugin[0] != 0u)
                             || (ui.craft_slot_plugin[1] != 0u)
                             || (ui.craft_slot_plugin[2] != 0u);
    UiNav_Pop(ui.ui_nav);
    ui.ui_dirty = true;
}

void ResetSdManageTrimFlow(AppUiState& ui, AppSharedState& shared)
{
    ui.sd_manage_edit_active = false;
    ui.sd_manage_edit_wait_for_load = false;
    ui.sd_manage_trim_rename_active = false;
    ui.sd_manage_trim_wait_for_worker = false;
    ui.sd_manage_trim_save_busy = false;
    ui.sd_manage_trim_choice_cursor = 0u;
    ui.sd_manage_trim_has_entry = false;
    ui.sd_manage_save_stem[0] = '\0';
    shared.sd_manage.sample = {};
    shared.sd_manage.edit = SampleEdit_Default(0);
}

void MaybeEnterSdManageTrimAfterLoad(AppUiState& ui, AppSharedState& shared)
{
    if(!ui.sd_manage_edit_active || !ui.sd_manage_edit_wait_for_load)
        return;
    if(UiNav_Active(ui.ui_nav) != UiScreenId::SdManageActionMenu)
        return;
    if(ui.sd.load_in_progress || ui.sd.sd_wav_load_busy)
        return;

    ui.sd_manage_edit_wait_for_load = false;
    if(std::strncmp(ui.sd.status, "LOADED", 6) != 0)
    {
        ResetSdManageTrimFlow(ui, shared);
        ui.ui_dirty = true;
        return;
    }

    ui.wave_edit_source = WaveEditSource::SdManage;
    if(UiNav_Push(ui.ui_nav, UiScreenId::PerformWaveEdit))
        ui.ui_dirty = true;
    else
    {
        ResetSdManageTrimFlow(ui, shared);
        ui.ui_dirty = true;
    }
}

void SyncActiveScreenEnter(UiScreenCtx& ctx, AppUiState& ui)
{
    const UiScreenId active_screen = UiNav_Active(ui.ui_nav);
    if(active_screen == ui.ui_active_screen)
        return;

    const UiScreenId prev_screen = ui.ui_active_screen;
    ui.ui_active_screen = active_screen;
    // Leaving CRAFT: stop the looping preview audition (1c made it loop until
    // stop_req) so it doesn't keep sounding on other screens.
    if(prev_screen == UiScreenId::CraftMenu && active_screen != UiScreenId::CraftMenu
       && ctx.shared)
    {
        ctx.shared->bake_preview.craft_preview_wanted.store(0, std::memory_order_release);
        ctx.shared->bake_preview.craft_chain_active.store(0, std::memory_order_release); // end live session
        ctx.shared->bake_preview.stop_req.store(1, std::memory_order_release);
    }
    const UiScreen& s = GetScreen(active_screen);
    if(s.OnEnter)
    {
        ctx.shift = ui.ui_lshift_held;
        ctx.lshift = ui.ui_lshift_held;
        ctx.rshift = ui.ui_rshift_held;
        s.OnEnter(ctx);
    }
    ui.ui_dirty = true;
}
} // namespace

bool UILogic::SchedulePendingNoteOff(uint8_t note, uint32_t due_ms)
{
    PendingNoteOff* free_slot = nullptr;
    for(size_t i = 0; i < kMaxPendingNoteOffs; ++i)
    {
        PendingNoteOff& slot = pending_note_offs_[i];
        if(slot.active && slot.note == note)
        {
            slot.due_ms = due_ms;
            return true;
        }
        if(!slot.active && free_slot == nullptr)
            free_slot = &slot;
    }

    if(free_slot == nullptr)
        return false;

    free_slot->active = true;
    free_slot->note = note;
    free_slot->due_ms = due_ms;
    return true;
}

void UILogic::CancelPendingNoteOff(uint8_t note)
{
    for(size_t i = 0; i < kMaxPendingNoteOffs; ++i)
    {
        if(pending_note_offs_[i].active && pending_note_offs_[i].note == note)
            pending_note_offs_[i].active = false;
    }
}

void UILogic::Init(DaisyPod& hw)
{
    controls_.hw = &hw;
    Controls_Init(controls_);
}

void UILogic::ScanInputsIsr(AppState& app)
{
    // Called from the 1 kHz input timer ISR. Debounces the encoders/buttons and
    // enqueues UiInputEvents into app.ui's SPSC ring (drained by the main loop in
    // UiTick). Short and allocation-free — safe for interrupt context.
    Controls_Tick(controls_, app.ui, System::GetNow());
}

void UILogic::ControlTick(DaisyPod& hw, AppState& app, Params& params, EventQueueSPSC& evtq)
{
    (void)hw;
    (void)params;
    AppSharedState& shared = app.shared;
    AppUiState& ui = app.ui;
    AppDiagnosticsState& diag = app.diag;
    const uint32_t now_ms = System::GetNow();
    // Hardware input scanning runs HERE in the main loop. (An experiment to move it
    // into a 1 kHz TIM5 ISR — UILogic::ScanInputsIsr — caused an intermittent boot
    // freeze: driving libDaisy's debouncers/ProcessDigitalControls from interrupt
    // context races with the boot-time OLED I2C-DMA bring-up. Reverted; the ISR
    // method is kept but unused. Input responsiveness now relies on the bounded
    // per-tick render cap keeping each main-loop iteration short.)
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
            if(ui.record_render_preview_note_active
               && ui.record_render_preview_note == slot.note)
            {
                ui.record_render_preview_note_active = false;
            }
            slot.active = false;
        }
        else
        {
            diag.queue_overflows.fetch_add(1, std::memory_order_relaxed);
            ui.ui_dirty = true;
        }
    }

    if(ui.record_render_phase == RecordRenderPhase::CaptureStarting)
    {
        if(!ui.record_render_all_notes_off_sent)
        {
            if(PushAllNotesOff(evtq, diag, ui))
                ui.record_render_all_notes_off_sent = true;
        }

        if(shared.recording.render_active.load(std::memory_order_acquire) != 0u)
        {
            ui.record_render_phase = RecordRenderPhase::Capturing;
            ui.record_render_capture_started_ms = now_ms;
            ui.record_render_note_on_due_ms = now_ms + 2u;
            ui.record_render_note_off_due_ms = ui.record_render_note_on_due_ms + ui.record_render_hold_ms;
            ui.ui_dirty = true;
        }
    }

    if(ui.record_render_phase == RecordRenderPhase::Capturing)
    {
        if(!ui.record_render_note_on_sent
           && static_cast<int32_t>(now_ms - ui.record_render_note_on_due_ms) >= 0)
        {
            const uint8_t note = ui.record_render_note;
            bool any_started = false;
            for(uint8_t layer = 0; layer < 1u; ++layer) // single layer
            {
                if(!RenderPreviewLayerEligible(params, shared, layer, note))
                    continue;
                if(PushPreviewNoteOn(evtq, diag, ui, note, kRenderPreviewVelocity, layer))
                    any_started = true;
            }
            if(any_started || !RenderPreviewAvailable(params, ui, shared))
                ui.record_render_note_on_sent = true;
        }

        if(ui.record_render_note_on_sent
           && !ui.record_render_note_off_sent
           && static_cast<int32_t>(now_ms - ui.record_render_note_off_due_ms) >= 0)
        {
            if(PushPreviewNoteOff(evtq, diag, ui, ui.record_render_note))
                ui.record_render_note_off_sent = true;
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

        }

        if(e.type == UiInputType::BtnDown)
        {
            if(e.id == kUiBtnLShift)
            {
                ui.ui_lshift_held = true;
                if(!blank_screen_active && !ui.ui_rshift_held)
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
            {
                ui.ui_rshift_held = true;
                if(ui.ui_lshift_held && ui.ui_parent_preview_active)
                {
                    ClearParentPreviewState(ui);
                    ui.ui_dirty = true;
                }
                if(rename_project_active)
                    ui.ui_dirty = true;
            }
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
            {
                ui.ui_rshift_held = false;
                if(rename_project_active)
                    ui.ui_dirty = true;
            }
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
                UiOverlay_Update(diag.overlay, diag, now_ms);
                ClearParentPreviewState(ui);
                ui.ui_dirty = true;
            }
            continue;
        }

        if(diag.overlay.modal_active)
        {
            if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
            {
                const uint8_t page_count = kDiagOverlayPageCount;
                uint8_t next_page = diag.overlay.page;
                if(e.value > 0)
                    next_page = static_cast<uint8_t>((next_page + 1u) % page_count);
                else
                    next_page = static_cast<uint8_t>((next_page + page_count - 1u) % page_count);
                diag.overlay.page = next_page;
                ui.ui_dirty = true;
                continue;
            }
            if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
            {
                DiagnosticsResetAudioCyclePeaks(diag);
                DiagnosticsResetVoiceCyclePeaks(diag);
                UiOverlay_ResetCpuRecent();
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
            // Toggle SHIFT menu from any screen.
            const UiScreenId active = UiNav_Active(ui.ui_nav);
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
                ui.shift_menu_bootloader_armed = false;
                ui.shift_menu_bootloader_arm_start_ms = 0;
                ui.shift_menu_bootloader_loading = false;
                ui.shift_menu_bootloader_loading_start_ms = 0;
                ui.shift_menu_firmware_update_active = false;
                // Cancel any pending SD delete mode when opening SHIFT.
                ui.sd_delete_mode = false;
                ui.sample_rename_active = false;
                ResetSdManageTrimFlow(ui, shared);
                UiNav_Push(ui.ui_nav, UiScreenId::ShiftMenu);
            }
            ui.ui_dirty = true;
            continue;
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

    MaybeReturnToCraftAfterLoad(ui);

    const UiScreenId active_screen = UiNav_Active(ui.ui_nav);
    if(active_screen != UiScreenId::SdBrowse)
    {
        ui.craft_browser_open = false;
        ui.craft_browser_wait_for_load = false;
    }
    SyncActiveScreenEnter(ctx, ui);

    if(recording.record_state == RecordUiState::Recording
       && shared.recording.rec_active.load(std::memory_order_acquire) == 0)
    {
        const uint32_t rec_len = shared.recording.rec_length.load(std::memory_order_acquire);
        if(rec_len > 0)
        {
            Sample& s = shared.recording.rec_sample;
            s.pcm = SdRecordBuffer();
            s.length = rec_len;
            s.sample_rate = 48000;
            s.root_key = 60;
            s.loop_start = 0;
            s.loop_end = rec_len;
            s.loop_enabled = false;

            SampleEdit edit = SampleEdit_Default(rec_len);
            shared.recording.rec_edit = edit;

            recording.record_slot = kRecordPreviewSampleIndex;
            PrepareSharedRecordReview(ui, recording, shared);
            Record_ApplyMonitorState(ui, recording, shared);
            UiNav_Push(ui.ui_nav, UiScreenId::RecordRenderReview);
            ui.ui_dirty = true;
        }
        else
        {
            recording.record_state = RecordUiState::SourceSelect;
            Record_ApplyMonitorState(ui, recording, shared);
            ui.ui_dirty = true;
        }
    }

    const bool record_review_active = (active_screen == UiScreenId::Record
                                       && recording.record_state == RecordUiState::Review);
    const bool render_review_active = (active_screen == UiScreenId::RecordRenderReview
                                       && ui.record_render_phase == RecordRenderPhase::Review);
    if(recording.record_preview_gate
       && shared.recording.preview_active.load(std::memory_order_acquire) == 0u)
    {
        recording.record_preview_gate = false;
    }
    if((record_review_active || render_review_active) && recording.record_preview_hold)
    {
        const Sample& s = shared.recording.rec_sample;
        if(s.pcm != nullptr && s.length > 0)
        {
            shared.recording.preview_start_req.store(1, std::memory_order_release);
            recording.record_preview_gate = true;
        }
        recording.record_preview_hold = false;
        recording.record_preview_restart = false;
    }
    if(!record_review_active && !render_review_active && recording.record_preview_gate)
    {
        shared.recording.preview_stop_req.store(1, std::memory_order_release);
        recording.record_preview_gate = false;
    }

    const bool record_render_active = (active_screen == UiScreenId::RecordRenderMenu);
    auto stop_render_preview = [&]() -> bool
    {
        if(!ui.record_render_preview_note_active)
            return true;

        const uint8_t note = ui.record_render_preview_note;
        if(PushPreviewNoteOff(evtq, diag, ui, note))
        {
            CancelPendingNoteOff(note);
            ui.record_render_preview_note_active = false;
            return true;
        }
        return false;
    };

    if(record_render_active)
    {
        if(ui.record_render_preview_trigger_pending)
        {
            if(stop_render_preview())
            {
                if(RenderPreviewAvailable(params, ui, shared))
                {
                    const uint8_t note = RenderPreviewMidiNote(ui);
                    bool any_started = false;
                    for(uint8_t layer = 0; layer < 1u; ++layer) // single layer
                    {
                        if(!RenderPreviewLayerEligible(params, shared, layer, note))
                            continue;
                        if(PushPreviewNoteOn(evtq, diag, ui, note, kRenderPreviewVelocity, layer))
                            any_started = true;
                    }

                    if(any_started && SchedulePendingNoteOff(note, now_ms + ui.record_render_hold_ms))
                    {
                        ui.record_render_preview_note_active = true;
                        ui.record_render_preview_note = note;
                    }
                    else if(any_started)
                    {
                        PushPreviewNoteOff(evtq, diag, ui, note);
                    }
                }
                ui.record_render_preview_trigger_pending = false;
            }
        }
    }
    else
    {
        ui.record_render_preview_trigger_pending = false;
        stop_render_preview();
    }

    if((ui.record_render_phase == RecordRenderPhase::CaptureStarting
        || ui.record_render_phase == RecordRenderPhase::Capturing)
       && shared.recording.render_done.load(std::memory_order_acquire) != 0u)
    {
        shared.recording.render_done.store(0, std::memory_order_release);
        shared.recording.render_active.store(0, std::memory_order_release);
        if(ui.record_render_note_on_sent && !ui.record_render_note_off_sent)
        {
            if(PushPreviewNoteOff(evtq, diag, ui, ui.record_render_note))
                ui.record_render_note_off_sent = true;
        }

        if(shared.recording.rec_length.load(std::memory_order_acquire) > 0u)
        {
            FinalizeRenderCapture(ui, recording, shared);
            ui.record_render_phase = RecordRenderPhase::Review;
            ui.record_render_review_focus = 0;
            ui.render_review_trim_entry = shared.recording.rec_edit;
            ui.render_review_trim_has_entry = false;
            if(UiNav_Active(ui.ui_nav) == UiScreenId::RecordRenderExecute)
            {
                UiNav_Pop(ui.ui_nav);
                UiNav_Push(ui.ui_nav, UiScreenId::RecordRenderReview);
            }
            Record_ApplyMonitorState(ui, recording, shared);
        }
        else
        {
            RecordRender_DiscardTemp(ui, recording, shared);
            std::snprintf(ui.record_render_status, sizeof(ui.record_render_status), "%s", "NO AUDIO");
            if(UiNav_Active(ui.ui_nav) == UiScreenId::RecordRenderExecute)
                UiNav_Pop(ui.ui_nav);
        }
        ui.ui_dirty = true;
    }

    // MIDI gate: silenced when (a) the Engine Trim screen is auditioning in
    // isolation, OR (b) the user is anywhere in the samples-menu subtree
    // (RECORD / CRAFT / BAKE / SD MANAGER and their children). The samples
    // gate is detected by scanning the nav stack for SamplesMenu, so new
    // sub-screens reachable from samples are automatically gated without
    // having to enumerate every screen id.
    //
    // Edge-detected because screen OnExit hooks aren't dispatched by the
    // router. On any gate edge we silence sounding notes. The engine-trim
    // falling-edge cleanup (win_preview_stop_req) fires only when engine-trim
    // specifically deactivates — tracked independently via
    // ui_engine_trim_was_active so it doesn't mis-fire on samples-subtree
    // transitions while engine-trim was never active.
    const bool engine_trim_active = (active_screen == UiScreenId::PerformWaveEdit)
                                    && (ui.wave_edit_source == WaveEditSource::PerformSlot);
    bool samples_subtree_active = false;
    for(uint8_t i = 0; i < ui.ui_nav.top; ++i)
    {
        if(ui.ui_nav.stack[i] == UiScreenId::SamplesMenu)
        {
            samples_subtree_active = true;
            break;
        }
    }
    const bool should_gate = engine_trim_active || samples_subtree_active;
    if(should_gate != ui.ui_midi_gate_active)
    {
        ui.ui_midi_gate_active = should_gate;
        PushAllNotesOff(evtq, diag, ui);
        ui.ui_dirty = true;
    }
    // Engine-trim falling edge → stop the one-shot window audition.
    if(ui.ui_engine_trim_was_active && !engine_trim_active)
        shared.recording.win_preview_stop_req.store(1, std::memory_order_release);
    ui.ui_engine_trim_was_active = engine_trim_active;

    // Seam-edit screen requests an all-notes-off on entry and on commit (where the
    // screen code cannot reach the event queue). Service it here.
    if(ui.ui_seam_silence_pending)
    {
        PushAllNotesOff(evtq, diag, ui);
        ui.ui_seam_silence_pending = false;
    }

    // Seam-edit playback-buffer swap. Waits a couple of ticks after AllNotesOff
    // so active voices fade out before we write to the shared playback buffer
    // (no audio-thread race once voices are silent).
    //   pending == 1 (ToLive):  memcpy raw -> playback, baked = 0 (live xfade)
    //   pending == 2 (ToBaked): re-bake raw -> playback, baked = 1 (single tap)
    if(ui.ui_seam_bake_pending != 0u)
    {
        if(ui.ui_seam_bake_delay_ticks > 0u)
        {
            --ui.ui_seam_bake_delay_ticks;
        }
        else
        {
            const uint8_t slot = ui.ui_seam_bake_layer & 1u;
            const Sample& samp = shared.sample.publish.sd_slots[slot];
            const uint32_t length = samp.length;
            if(length > 0u)
            {
                int16_t* raw      = SdSampleRawBuffer(slot);
                int16_t* playback = SdSampleBuffer(slot);
                if(ui.ui_seam_bake_pending == 1u)
                {
                    std::memcpy(playback, raw, static_cast<size_t>(length) * sizeof(int16_t));
                    shared.sample.publish.sd_layer_seam_baked[slot].store(
                        0u, std::memory_order_release);
                }
                else
                {
                    const SampleEdit& edit = shared.sample.edit.sd_edit_slots[slot];
                    const bool baked = BakeLoopSeamToBuffer(raw,
                                                            length,
                                                            edit.start_frame,
                                                            edit.end_frame,
                                                            ui.ui_seam_bake_amount,
                                                            ui.ui_seam_bake_shape,
                                                            48000.0f,
                                                            playback);
                    shared.sample.publish.sd_layer_seam_baked[slot].store(
                        baked ? 1u : 0u, std::memory_order_release);
                }
            }
            ui.ui_seam_bake_pending = 0u;
        }
    }

    // Safety net: monophonic seam audition must never outlive the seam-edit screen.
    // Force polyphony back on whenever we are not on that screen, regardless of how
    // it was exited.
    if(active_screen != UiScreenId::PerformSeamEdit && ui.ui_seam_audition_active)
        ui.ui_seam_audition_active = false;

    // Trim-window auditioning is handled entirely by the self-contained
    // win_preview bridge (Button2 press-toggle in PerformWaveEdit_OnEvent), which
    // plays only [start,end) and auto-stops. No main-thread gating needed here.

    shift_held = ui.ui_lshift_held;
    UiOverlay_Update(diag.overlay, diag, now_ms);

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
       && (worker.ui_req_active == UiReqType::SaveRenderedWavCurrent
           || worker.ui_req_active == UiReqType::SaveRenderedWavNamed
           || worker.ui_req_active == UiReqType::SaveSdManageTrimNamed
           || worker.ui_req_active == UiReqType::ReplaceSdManageTrimCurrent))
        worker_budget_us = 6000;
    UiWorker_Tick(app.ui,
                  app.project,
                  app.engine,
                  app.shared,
                  app.worker,
                  params,
                  now_ms,
                  worker_budget_us);

    MaybeReturnToCraftAfterLoad(ui);
    MaybeEnterSdManageTrimAfterLoad(ui, shared);
    SyncActiveScreenEnter(ctx, ui);

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

    if(ui.project_delete_pending
       && worker.ui_req_done_count != ui.project_delete_pending_done_count)
    {
        ui.project_delete_pending = false;
        if(worker.ui_req_result < 0)
        {
            if(UiNav_Active(ui.ui_nav) != UiScreenId::ProjectStatus)
                UiNav_Push(ui.ui_nav, UiScreenId::ProjectStatus);
        }
        else
        {
            RebuildVisibleProjectOrderFromMetadata(ui, app.project);
        }
        ui.ui_dirty = true;
    }

    if(ui.render_sample_rename_wait_for_worker)
    {
        if(ui.sd.save_in_progress)
        {
            ui.render_sample_rename_wait_for_worker = false;
            ui.render_sample_rename_active = false;
            if(UiNav_Active(ui.ui_nav) == UiScreenId::RenameProject)
                UiNav_Pop(ui.ui_nav);
            ui.ui_dirty = true;
        }
        else if(!worker.ui_req_busy)
        {
            ui.render_sample_rename_wait_for_worker = false;
            ui.record_render_phase = RecordRenderPhase::Review;
            if(ui.sd.save_status[0] != '\0')
            {
                std::snprintf(ui.record_render_status,
                              sizeof(ui.record_render_status),
                              "%s",
                              ui.sd.save_status);
            }
            ui.ui_dirty = true;
        }
    }

    if(ui.record_render_phase == RecordRenderPhase::SaveWait
       && !ui.render_sample_rename_wait_for_worker
       && !ui.sd.save_in_progress
       && !worker.ui_req_busy)
    {
        const bool save_ok = (std::strncmp(ui.sd.save_status, "SAVED", 5) == 0);
        if(save_ok)
        {
            const bool physical_review = (ReviewParentScreen(ui) == UiScreenId::Record);
            RecordRender_DiscardTemp(ui, recording, shared);
            ui.render_sample_rename_active = false;
            ui.record_render_save_stem[0] = '\0';
            ui.record_render_status[0] = '\0';
            if(UiNav_Active(ui.ui_nav) == UiScreenId::RecordRenderReview)
                UiNav_Pop(ui.ui_nav);
            if(physical_review)
            {
                ui.record_menu_index = recording.record_source_index & 1u;
                if(UiNav_Active(ui.ui_nav) == UiScreenId::Record)
                    UiNav_Pop(ui.ui_nav);
            }
        }
        else
        {
            ui.record_render_phase = RecordRenderPhase::Review;
            if(ui.sd.save_status[0] != '\0')
            {
                std::snprintf(ui.record_render_status,
                              sizeof(ui.record_render_status),
                              "%s",
                              ui.sd.save_status);
            }
        }
        ui.ui_dirty = true;
    }

    if(ui.sd_manage_trim_wait_for_worker && !ui.sd.save_in_progress && !worker.ui_req_busy)
    {
        ui.sd_manage_trim_wait_for_worker = false;
        ui.sd_manage_trim_save_busy = false;
        const bool save_ok = (std::strncmp(ui.sd.save_status, "SAVED", 5) == 0)
                             || (std::strncmp(ui.sd.save_status, "REPLACED", 8) == 0);
        if(save_ok)
        {
            while(UiNav_Active(ui.ui_nav) != UiScreenId::SdManageMenu)
            {
                if(!UiNav_Pop(ui.ui_nav))
                    break;
            }
            ResetSdManageTrimFlow(ui, shared);
        }
        ui.ui_dirty = true;
    }

    if(active_screen == UiScreenId::RecordRenderExecute
       && (ui.record_render_phase == RecordRenderPhase::CaptureStarting
           || ui.record_render_phase == RecordRenderPhase::Capturing))
    {
        ui.ui_dirty = true;
    }
}
