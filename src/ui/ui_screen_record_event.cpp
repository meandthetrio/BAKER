#include "ui_screens_internal.h"
#include "ui_screen_record_internal.h"

#include "app_state.h"
#include "sd_browser_state.h"
#include "ui_input.h"
#include "ui_requests.h"

bool Record_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    AppState& app = *ctx.app;
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
        if(app.recording.record_state == RecordUiState::SourceSelect)
        {
            app.recording.record_source_index = wrap2(static_cast<int>(app.recording.record_source_index) + e.value);
            app.shared.rec_source_sel.store(app.recording.record_source_index & 1u, std::memory_order_release);
            app.ui.ui_dirty = true;
            return true;
        }
        if(app.recording.record_state == RecordUiState::TargetSelect)
        {
            app.recording.record_target_index = wrap2(static_cast<int>(app.recording.record_target_index) + e.value);
            app.ui.ui_dirty = true;
            return true;
        }
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        if(app.recording.record_state == RecordUiState::Review)
        {
            app.recording.record_state = RecordUiState::BackConfirm;
            Record_StopPreview(app);
            app.ui.ui_dirty = true;
            return true;
        }
        if(app.recording.record_state == RecordUiState::BackConfirm)
        {
            app.recording.record_state = RecordUiState::Review;
            app.ui.ui_dirty = true;
            return true;
        }
        if(app.recording.record_state == RecordUiState::Recording)
        {
            app.shared.rec_stop_req.store(1, std::memory_order_release);
            app.ui.ui_dirty = true;
            return true;
        }
        if(app.recording.record_state == RecordUiState::Armed)
        {
            app.recording.record_state = RecordUiState::SourceSelect;
            app.shared.rec_monitor_enable.store(0, std::memory_order_release);
            app.recording.record_anim_start_ms = -1.0;
            app.ui.ui_dirty = true;
            return true;
        }
        if(app.recording.record_state == RecordUiState::TargetSelect)
        {
            app.recording.record_state = RecordUiState::Review;
            app.ui.ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        if(app.recording.record_state == RecordUiState::Review)
        {
            app.recording.record_preview_hold = true;
            app.ui.ui_dirty = true;
            return true;
        }
    }
    else if(e.type == UiInputType::BtnUp && e.id == kUiBtnPod2)
    {
        if(app.recording.record_state == RecordUiState::Review)
        {
            Record_StopPreview(app);
            app.ui.ui_dirty = true;
            return true;
        }
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(app.recording.record_state == RecordUiState::SourceSelect)
        {
            app.recording.record_state = RecordUiState::Armed;
            app.shared.rec_source_sel.store(app.recording.record_source_index & 1u, std::memory_order_release);
            app.shared.rec_monitor_enable.store(1, std::memory_order_release);
            app.recording.record_anim_start_ms = -1.0;
            app.ui.ui_dirty = true;
            return true;
        }
        if(app.recording.record_state == RecordUiState::Armed)
        {
            app.recording.record_countdown_start_ms = ctx.now_ms;
            app.recording.record_state = RecordUiState::Countdown;
            app.shared.rec_monitor_enable.store(1, std::memory_order_release);
            app.recording.record_anim_start_ms = -1.0;
            app.ui.ui_dirty = true;
            return true;
        }
        if(app.recording.record_state == RecordUiState::Recording)
        {
            app.shared.rec_stop_req.store(1, std::memory_order_release);
            app.ui.ui_dirty = true;
            return true;
        }
        if(app.recording.record_state == RecordUiState::Review)
        {
            app.recording.record_target_index = 0;
            app.recording.record_state = RecordUiState::TargetSelect;
            app.ui.ui_dirty = true;
            return true;
        }
        if(app.recording.record_state == RecordUiState::TargetSelect)
        {
            if(app.recording.record_target_index == 0)
            {
                UiReq req{UiReqType::SaveRenderedWavCurrent, 0, 0};
                if(UiReq_Push(app, req))
                {
                    SdBrowser_SetSaveStatus(app.ui.sd, "SAVING");
                    app.ui.sd.save_progress = 0;
                    app.ui.sd.save_in_progress = true;
                    app.recording.record_state = RecordUiState::SaveWait;
                }
                else
                {
                    SdBrowser_SetSaveStatus(app.ui.sd, "SAVE ERR");
                    app.recording.record_state = RecordUiState::Review;
                }
            }
            else
            {
                Record_StopPreview(app);
                app.recording.record_state = RecordUiState::SourceSelect;
            }
            app.ui.ui_dirty = true;
            return true;
        }
        if(app.recording.record_state == RecordUiState::BackConfirm)
        {
            Record_StopPreview(app);
            app.shared.rec_stop_req.store(1, std::memory_order_release);
            app.shared.rec_active.store(0, std::memory_order_release);
            app.shared.rec_length.store(0, std::memory_order_release);
            app.shared.rec_monitor_enable.store(0, std::memory_order_release);
            app.recording.record_state = RecordUiState::SourceSelect;
            app.ui.ui_dirty = true;
            return true;
        }
    }

    return false;
}

void Record_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    AppState& app = *ctx.app;
    app.recording.record_state = RecordUiState::SourceSelect;
    app.recording.record_source_index = 0;
    app.recording.record_target_index = 0;
    app.recording.record_slot = app.engine.perform_layer & 1u;
    app.recording.record_anim_start_ms = -1.0;
    app.shared.rec_source_sel.store(app.recording.record_source_index & 1u, std::memory_order_release);
    app.shared.rec_monitor_enable.store(0, std::memory_order_release);
    Record_StopPreview(app);
    app.shared.rec_start_req.store(0, std::memory_order_release);
    app.shared.rec_stop_req.store(0, std::memory_order_release);
    app.ui.ui_dirty = true;
}

void Record_OnExit(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    AppState& app = *ctx.app;
    Record_StopPreview(app);
    app.shared.rec_monitor_enable.store(0, std::memory_order_release);
    app.recording.record_anim_start_ms = -1.0;
    app.shared.rec_stop_req.store(1, std::memory_order_release);
}
