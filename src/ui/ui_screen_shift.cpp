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
    ShiftProjectSlot,
    ShiftSaveProject,
    ShiftLoadProject,
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

        if(!ui.shift_menu_edit_volume
           && e.id == kUiEncExt
           && ui.shift_menu_cursor == ShiftProjectSlot
           && e.value != 0)
        {
            const int next = static_cast<int>(project.current_project_slot) + e.value;
            project.current_project_slot = ProjectActions_WrapSlot(next);
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
        if(ui.shift_menu_cursor == ShiftLoadProject)
            return ProjectActions_TriggerRequest(ui,
                                                 project,
                                                 worker,
                                                 UiReqType::LoadProject,
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
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;
    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    const int screen_w = (int)d.Width();
    char status[16];
    BuildStatus(shared, status, sizeof(status));
    UiDraw_Header(d, layout, "SETTINGS", status);

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

    // Settings rows: DELETE / OUTPUT VOL / PROJECT SLOT / SAVE PROJECT / LOAD PROJECT.
    const int row_y0 = layout.y_body;
    const int row_h = layout.line_h;

    for(int i = 0; i < ShiftCount; ++i)
    {
        const bool sel = (ui.shift_menu_cursor == (uint8_t)i);
        const int y = row_y0 + i * row_h;
        const int x0 = layout.x;
        const int label_x = x0 + 1;
        const int label_y = y + 1;
        if(i == ShiftDelete)
        {
            if(sel) DrawRencFocusString6x8(d, "DELETE", label_x, label_y);
            else
            {
                d.SetCursor(label_x, label_y);
                d.WriteString("DELETE", Font_6x8, true);
            }
        }
        else if(i == ShiftVolume)
        {
            const char* label = ui.shift_menu_edit_volume ? "OUTPUT VOL*" : "OUTPUT VOL";
            if(sel) DrawRencFocusString6x8(d, label, label_x, label_y);
            else
            {
                d.SetCursor(label_x, label_y);
                d.WriteString(label, Font_6x8, true);
            }

            // Right-aligned value.
            char buf[8];
            int  val_len = 3;
            if(vol_pct == 100u)
            {
                std::snprintf(buf, sizeof(buf), "UNITY");
                val_len = 5;
            }
            else if(vol_pct > 100u)
            {
                std::snprintf(buf, sizeof(buf), "+%3lu", (unsigned long)vol_pct);
                val_len = 4;
            }
            else
            {
                std::snprintf(buf, sizeof(buf), "%3lu", (unsigned long)vol_pct);
                val_len = 3;
            }

            const int val_w = 6 * val_len;
            d.SetCursor(screen_w - val_w - 1, y + 1);
            d.WriteString(buf, Font_6x8, true);
        }
        else if(i == ShiftProjectSlot)
        {
            if(sel) DrawRencFocusString6x8(d, "PROJECT SLOT", label_x, label_y);
            else
            {
                d.SetCursor(label_x, label_y);
                d.WriteString("PROJECT SLOT", Font_6x8, true);
            }

            char buf[8];
            std::snprintf(buf,
                          sizeof(buf),
                          "%02u",
                          static_cast<unsigned>(project.current_project_slot + 1u));
            d.SetCursor(screen_w - 12 - 1, y + 1);
            d.WriteString(buf, Font_6x8, true);
        }
        else if(i == ShiftSaveProject)
        {
            if(sel) DrawRencFocusString6x8(d, "SAVE PROJECT", label_x, label_y);
            else
            {
                d.SetCursor(label_x, label_y);
                d.WriteString("SAVE PROJECT", Font_6x8, true);
            }
        }
        else if(i == ShiftLoadProject)
        {
            if(sel) DrawRencFocusString6x8(d, "LOAD PROJECT", label_x, label_y);
            else
            {
                d.SetCursor(label_x, label_y);
                d.WriteString("LOAD PROJECT", Font_6x8, true);
            }
        }
    }

    const char* hint = ui.shift_menu_edit_volume ? "L:NAV R:CHG P2:BACK"
                        : (ui.shift_menu_cursor == ShiftProjectSlot) ? "L:NAV R:CHG/CLK"
                        : "L:NAV R:SEL";
    UiDraw_Footer(d, layout, hint);
}
