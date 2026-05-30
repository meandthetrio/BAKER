#include "ui_screens_internal.h"
#include "ui_screen_record_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "sd_browser_state.h"
#include "storage_limits.h"
#include "ui_input.h"
#include "ui_requests.h"

bool Record_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;

    AppUiState& ui = *ctx.ui;
    AppRecordingState& recording = *ctx.recording;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;
    if(ctx.shift)
        return false;

    auto wrap2 = [](int v) -> uint8_t
    {
        while(v < 0)
            v += 2;
        while(v >= 2)
            v -= 2;
        return static_cast<uint8_t>(v);
    };

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        if(recording.record_state == RecordUiState::SourceSelect)
        {
            recording.record_source_index = wrap2(static_cast<int>(recording.record_source_index) + e.value);
            shared.recording.rec_source_sel.store(recording.record_source_index & 1u, std::memory_order_release);
            ui.ui_dirty = true;
            return true;
        }
        if(recording.record_state == RecordUiState::TargetSelect)
        {
            recording.record_target_index = wrap2(static_cast<int>(recording.record_target_index) + e.value);
            ui.ui_dirty = true;
            return true;
        }
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        if(recording.record_state == RecordUiState::Review)
        {
            recording.record_state = RecordUiState::BackConfirm;
            Record_StopPreview(recording, shared);
            ui.ui_dirty = true;
            return true;
        }
        if(recording.record_state == RecordUiState::BackConfirm)
        {
            recording.record_state = RecordUiState::Review;
            ui.ui_dirty = true;
            return true;
        }
        if(recording.record_state == RecordUiState::Recording)
        {
            shared.recording.rec_stop_req.store(1, std::memory_order_release);
            ui.ui_dirty = true;
            return true;
        }
        if(recording.record_state == RecordUiState::Armed)
        {
            if(ui.record_menu_armed_back_returns_to_menu)
            {
                Record_StopPreview(recording, shared);
                recording.record_anim_start_ms = -1.0;
                shared.recording.rec_stop_req.store(1, std::memory_order_release);
                ui.record_menu_armed_back_returns_to_menu = false;
                Record_ApplyMonitorState(ui, recording, shared);
                ui.ui_dirty = true;
                return false;
            }
            recording.record_state = RecordUiState::SourceSelect;
            recording.record_anim_start_ms = -1.0;
            Record_ApplyMonitorState(ui, recording, shared);
            ui.ui_dirty = true;
            return true;
        }
        if(recording.record_state == RecordUiState::TargetSelect)
        {
            recording.record_state = RecordUiState::Review;
            ui.ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        if(recording.record_state == RecordUiState::Review)
        {
            recording.record_preview_hold = true;
            recording.record_preview_restart = recording.record_preview_gate;
            ui.ui_dirty = true;
            return true;
        }
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(recording.record_state == RecordUiState::SourceSelect)
        {
            recording.record_state = RecordUiState::Armed;
            shared.recording.rec_source_sel.store(recording.record_source_index & 1u, std::memory_order_release);
            recording.record_anim_start_ms = -1.0;
            Record_ApplyMonitorState(ui, recording, shared);
            ui.ui_dirty = true;
            return true;
        }
        if(recording.record_state == RecordUiState::Armed)
        {
            recording.record_countdown_start_ms = ctx.now_ms;
            recording.record_state = RecordUiState::Countdown;
            recording.record_anim_start_ms = -1.0;
            ui.record_menu_armed_back_returns_to_menu = false;
            Record_ApplyMonitorState(ui, recording, shared);
            ui.ui_dirty = true;
            return true;
        }
        if(recording.record_state == RecordUiState::Recording)
        {
            shared.recording.rec_stop_req.store(1, std::memory_order_release);
            ui.ui_dirty = true;
            return true;
        }
        if(recording.record_state == RecordUiState::Review)
        {
            recording.record_target_index = 0;
            recording.record_state = RecordUiState::TargetSelect;
            ui.ui_dirty = true;
            return true;
        }
        if(recording.record_state == RecordUiState::TargetSelect)
        {
            if(recording.record_target_index == 0)
            {
                UiReq req{UiReqType::SaveRenderedWavCurrent, 1, 0};
                if(UiReq_Push(ui, worker, req))
                {
                    SdBrowser_SetSaveStatus(ui.sd, "SAVING");
                    ui.sd.save_progress = 0;
                    ui.sd.save_in_progress = true;
                    recording.record_state = RecordUiState::SaveWait;
                }
                else
                {
                    SdBrowser_SetSaveStatus(ui.sd, "SAVE ERR");
                    recording.record_state = RecordUiState::Review;
                }
            }
            else
            {
                Record_StopPreview(recording, shared);
                recording.record_state = RecordUiState::SourceSelect;
                ui.record_menu_armed_back_returns_to_menu = false;
                Record_ApplyMonitorState(ui, recording, shared);
            }
            ui.ui_dirty = true;
            return true;
        }
        if(recording.record_state == RecordUiState::BackConfirm)
        {
            Record_StopPreview(recording, shared);
            shared.recording.rec_stop_req.store(1, std::memory_order_release);
            shared.recording.rec_active.store(0, std::memory_order_release);
            shared.recording.rec_length.store(0, std::memory_order_release);
            recording.record_state = RecordUiState::SourceSelect;
            ui.record_menu_armed_back_returns_to_menu = false;
            Record_ApplyMonitorState(ui, recording, shared);
            ui.ui_dirty = true;
            return true;
        }
    }

    return false;
}

void Record_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;
    AppUiState& ui = *ctx.ui;
    AppRecordingState& recording = *ctx.recording;
    AppSharedState& shared = *ctx.shared;
    recording.record_state = RecordUiState::SourceSelect;
    recording.record_source_index = 0;
    recording.record_target_index = 0;
    recording.record_slot = kRecordPreviewSampleIndex;
    recording.record_anim_start_ms = -1.0;
    Record_StopPreview(recording, shared);
    shared.recording.rec_start_req.store(0, std::memory_order_release);
    shared.recording.rec_stop_req.store(0, std::memory_order_release);

    if(ui.record_menu_source_override_active)
    {
        recording.record_source_index = ui.record_menu_source_override & 1u;
        recording.record_state = RecordUiState::Armed;
        shared.recording.rec_source_sel.store(recording.record_source_index & 1u,
                                              std::memory_order_release);
        ui.record_menu_source_override_active = false;
        ui.record_menu_armed_back_returns_to_menu = true;
        Record_ApplyMonitorState(ui, recording, shared);
    }
    else
    {
        recording.record_state = RecordUiState::SourceSelect;
        shared.recording.rec_source_sel.store(recording.record_source_index & 1u,
                                              std::memory_order_release);
        ui.record_menu_armed_back_returns_to_menu = false;
        Record_ApplyMonitorState(ui, recording, shared);
    }

    ui.ui_dirty = true;
}

void Record_OnExit(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;
    AppRecordingState& recording = *ctx.recording;
    AppSharedState& shared = *ctx.shared;
    Record_StopPreview(recording, shared);
    recording.record_state = RecordUiState::SourceSelect;
    Record_ApplyMonitorState(*ctx.ui, recording, shared);
    recording.record_anim_start_ms = -1.0;
    shared.recording.rec_stop_req.store(1, std::memory_order_release);
    ctx.ui->record_menu_armed_back_returns_to_menu = false;
}
