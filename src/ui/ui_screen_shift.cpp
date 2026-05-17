#include "ui_screens_internal.h"

#include <cstdio>

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "oled_pager.h"
#include "params.h"
#include "project_actions.h"
#include "sd_browser_state.h"
#include "ui_input.h"
#include "ui_layout.h"
#include "ui_requests.h"

enum ShiftMenuItem : uint8_t
{
    ShiftDelete = 0,
    ShiftVolume,
    ShiftSaveProject,
    ShiftCount
};

void ShiftMenu_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;
    // Returning to SHIFT should cancel any SD delete mode.
    ctx.ui->sd_delete_mode = false;
    ctx.ui->shift_menu_edit_volume = false;
}

bool ShiftMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    if(ctx.shift)
        return false;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;

    if(e.type == UiInputType::EncDelta)
    {
        // R encoder turn adjusts value when editing VOLUME.
        if(ui.shift_menu_edit_volume && e.id == kUiEncExt)
        {
            if(!ctx.params)
                return true;
            auto& t = ctx.params->EditTargets();
            // Option B: allow master boost up to 200% (2.0)
            static constexpr float kMasterLevelMax = 2.0f;

            // Cuz-like feel: time-based acceleration.
            // Fast turns -> bigger jumps.
            static uint32_t s_last_t_ms = 0;
            const uint32_t now_ms = e.t_ms;
            const uint32_t dt_ms  = (s_last_t_ms == 0) ? 999u : (now_ms - s_last_t_ms);
            s_last_t_ms = now_ms;

            float accel = 1.0f;
            if(dt_ms <= 25)       accel = 10.0f;
            else if(dt_ms <= 50)  accel = 6.0f;
            else if(dt_ms <= 90)  accel = 3.0f;
            else if(dt_ms <= 140) accel = 2.0f;

            // Base step: 1% per detent at accel=1.0
            const float base_step = 0.01f;

            float next = t.master_level + (float)e.value * base_step * accel;

            if(next < 0.0f) next = 0.0f;
            if(next > kMasterLevelMax) next = kMasterLevelMax;

            t.master_level = next;
            ctx.params->PublishTargets();
            ui.ui_dirty = true;
            return true;
        }

        // L encoder turn scrolls between settings rows when not editing.
        if(!ui.shift_menu_edit_volume && e.id == kUiEncPod)
        {
            uint8_t cur = ui.shift_menu_cursor;
            if(e.value > 0)
                cur = (cur + 1u < ShiftCount) ? static_cast<uint8_t>(cur + 1u) : cur;
            else if(e.value < 0)
                cur = (cur > 0u) ? static_cast<uint8_t>(cur - 1u) : cur;

            if(cur != ui.shift_menu_cursor)
            {
                ui.shift_menu_cursor = cur;
                ui.ui_dirty = true;
            }
            return true;
        }
    }
    // EXT encoder click = select.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.shift_menu_cursor == ShiftDelete)
        {
            // DELETE: enter SD browser in delete mode.
            ui.sd_delete_mode = true;
            ui.shift_menu_edit_volume = false;
            SdBrowser_SetStatus(ui.sd, "DEL:SELECT");
            UiNav_Push(ui.ui_nav, UiScreenId::SdBrowse);
            ui.ui_dirty = true;
            return true;
        }
        if(ui.shift_menu_cursor == ShiftVolume)
        {
            if(ui.shift_menu_edit_volume && ctx.params)
            {
                auto& t = ctx.params->EditTargets();
                t.master_level = 1.0f; // UNITY
                ctx.params->PublishTargets();
                ui.ui_dirty = true;
                return true;
            }
            // VOLUME: toggle edit mode.
            ui.shift_menu_edit_volume = !ui.shift_menu_edit_volume;
            ui.ui_dirty = true;
            return true;
        }
        if(ui.shift_menu_cursor == ShiftSaveProject)
            return ProjectActions_TriggerRequest(ui,
                                                 project,
                                                 worker,
                                                 UiReqType::SaveProject,
                                                 project.current_project_slot);
        return true;
    }

    // L encoder click backs out one level when editing volume.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        if(ui.shift_menu_edit_volume)
        {
            ui.shift_menu_edit_volume = false;
            ui.ui_dirty = true;
            return true; // consume so router doesn't pop screen
        }
    }

    // POD2 also cancels volume edit (optional)
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        if(ui.shift_menu_edit_volume)
        {
            ui.shift_menu_edit_volume = false;
            ui.ui_dirty = true;
            return true;
        }
    }

    return false;
}

void ShiftMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    OledPager& d = *ctx.display;
    d.Fill(false);

    const int screen_w = (int)d.Width();

    // Compute volume percent for display (0..200 with boost).
    uint32_t vol_pct = 0;
    if(ctx.params)
    {
        static constexpr float kMasterLevelMax = 2.0f;
        float v = ctx.params->TargetsForUI().master_level;

        if(v < 0.0f) v = 0.0f;
        if(v > kMasterLevelMax) v = kMasterLevelMax;

        vol_pct = (uint32_t)(v * 100.0f + 0.5f);
        if(vol_pct > 200u)
            vol_pct = 200u;
    }

    static constexpr int kRowPitch = Font5x7::H + 2;
    const int total_h = static_cast<int>(ShiftCount) * Font5x7::H
                        + static_cast<int>(ShiftCount - 1u) * 2;
    const int row_y0 = (static_cast<int>(d.Height()) - total_h) / 2;

    for(int i = 0; i < ShiftCount; ++i)
    {
        const bool sel = (ui.shift_menu_cursor == (uint8_t)i);
        const int label_y = row_y0 + i * kRowPitch;
        const int label_x = 1;
        if(i == ShiftDelete)
        {
            if(sel) DrawRencFocusTinyString(d, "DELETE", label_x, label_y);
            else DrawTinyString(d, "DELETE", label_x, label_y, true);
        }
        else if(i == ShiftVolume)
        {
            const char* label = "OUTPUT VOL";
            if(sel) DrawRencFocusTinyString(d, label, label_x, label_y);
            else DrawTinyString(d, label, label_x, label_y, true);

            // Right-aligned value.
            char buf[8];
            if(vol_pct == 100u)
            {
                std::snprintf(buf, sizeof(buf), "UNITY");
            }
            else if(vol_pct > 100u)
            {
                std::snprintf(buf, sizeof(buf), "+%3lu", (unsigned long)vol_pct);
            }
            else
            {
                std::snprintf(buf, sizeof(buf), "%3lu", (unsigned long)vol_pct);
            }

            const int val_w = TinyStringWidth(buf);
            DrawTinyString(d, buf, screen_w - val_w - 1, label_y, true);
        }
        else if(i == ShiftSaveProject)
        {
            if(sel) DrawRencFocusTinyString(d, "SAVE PROJECT", label_x, label_y);
            else DrawTinyString(d, "SAVE PROJECT", label_x, label_y, true);
        }
    }
}
