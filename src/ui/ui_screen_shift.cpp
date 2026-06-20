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
#include "system.h"
#include "ui_input.h"
#include "ui_layout.h"

// MIDI panic helper from main.cpp — pushes an AllNotesOff event to the
// audio engine to clear any stuck voices.
extern "C" void Pod_MidiPanic();
#include "ui_requests.h"

enum ShiftMenuItem : uint8_t
{
    ShiftSaveProject = 0,
    ShiftVolume,
    ShiftMicMonitor,
    ShiftNormalize,
    ShiftFirmwareUpdate,
    ShiftMidiPanic,
    ShiftCount
};

static constexpr uint32_t kShiftBootloaderHoldMs = 2000u;
static constexpr uint32_t kShiftBootloaderLoadingOverlayMs = 3000u;

static void DrawFillOnlyTinyString(OledPager& d, const char* str, int x, int y)
{
    if(!str)
        return;
    const int w = TinyStringWidth(str);
    int x0 = x - 2;
    int y0 = y - 2;
    int x1 = x + w + 1;
    int y1 = y + Font5x7::H + 1;
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 > 127) x1 = 127;
    if(y1 > 63) y1 = 63;
    d.DrawRect(x0, y0, x1, y1, true, true);
    DrawTinyString(d, str, x, y, false);
}

static void DrawOutlineTinyString(OledPager& d, const char* str, int x, int y)
{
    if(!str)
        return;
    const int w = TinyStringWidth(str);
    int x0 = x - 2;
    int y0 = y - 2;
    int x1 = x + w + 1;
    int y1 = y + Font5x7::H + 1;
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 > 127) x1 = 127;
    if(y1 > 63) y1 = 63;
    d.DrawRect(x0, y0, x1, y1, true, false);
    DrawTinyString(d, str, x, y, true);
}

static void ClearShiftBootloaderState(AppUiState& ui)
{
    ui.shift_menu_firmware_update_active = false;
    ui.shift_menu_bootloader_armed = false;
    ui.shift_menu_bootloader_arm_start_ms = 0;
    ui.shift_menu_bootloader_loading = false;
    ui.shift_menu_bootloader_loading_start_ms = 0;
}

static void CancelShiftBootloaderArm(AppUiState& ui)
{
    ui.shift_menu_bootloader_armed = false;
    ui.shift_menu_bootloader_arm_start_ms = 0;
}

static void BeginShiftBootloaderLoading(AppUiState& ui, uint32_t now_ms)
{
    CancelShiftBootloaderArm(ui);
    ui.shift_menu_bootloader_loading = true;
    ui.shift_menu_bootloader_loading_start_ms = now_ms;
}

void ShiftMenu_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;
    // Returning to SHIFT should cancel any SD delete mode.
    ctx.ui->sd_delete_mode = false;
    ClearShiftBootloaderState(*ctx.ui);
}

bool ShiftMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    if(ctx.shift)
        return false;

    AppUiState& ui = *ctx.ui;
    if(ui.shift_menu_bootloader_loading)
    {
        if(e.type == UiInputType::BtnDown
           && (e.id == kUiBtnPodEnc || e.id == kUiBtnPod2))
        {
            ui.shift_menu_bootloader_loading = false;
            ui.shift_menu_bootloader_loading_start_ms = 0;
            ui.shift_menu_firmware_update_active = false;
            ui.shift_menu_bootloader_armed = false;
            ui.shift_menu_bootloader_arm_start_ms = 0;
            ui.ui_dirty = true;
            return true;
        }
        return true;
    }

    AppProjectState& project = *ctx.project;
    AppWorkerState& worker = *ctx.worker;

    if(ui.shift_menu_firmware_update_active)
    {
        if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
        {
            ui.shift_menu_bootloader_armed = true;
            ui.shift_menu_bootloader_arm_start_ms = e.t_ms;
            ui.ui_dirty = true;
            return true;
        }
        if(e.type == UiInputType::BtnUp && e.id == kUiBtnExtEnc)
        {
            if(ui.shift_menu_bootloader_armed)
            {
                CancelShiftBootloaderArm(ui);
                ui.ui_dirty = true;
            }
            return true;
        }
        if(e.type == UiInputType::BtnDown
           && (e.id == kUiBtnPodEnc || e.id == kUiBtnPod2))
        {
            if(ui.shift_menu_bootloader_armed)
            {
                CancelShiftBootloaderArm(ui);
            }
            else
            {
                ui.shift_menu_firmware_update_active = false;
            }
            ui.ui_dirty = true;
            return true;
        }
        if(e.type == UiInputType::EncDelta && e.id == kUiEncExt
           && ui.shift_menu_bootloader_armed)
        {
            CancelShiftBootloaderArm(ui);
            ui.ui_dirty = true;
            return true;
        }
        return true;
    }

    if(e.type == UiInputType::EncDelta)
    {
        if(ui.shift_menu_bootloader_armed && e.id == kUiEncExt)
        {
            CancelShiftBootloaderArm(ui);
            ui.ui_dirty = true;
            return true;
        }

        // R encoder turn adjusts OUTPUT VOL directly while that row is focused.
        if(ui.shift_menu_cursor == ShiftVolume && e.id == kUiEncExt)
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

        // L encoder turn scrolls between settings rows.
        if(e.id == kUiEncPod)
        {
            uint8_t cur = ui.shift_menu_cursor;
            if(e.value > 0)
                cur = (cur + 1u < ShiftCount) ? static_cast<uint8_t>(cur + 1u) : cur;
            else if(e.value < 0)
                cur = (cur > 0u) ? static_cast<uint8_t>(cur - 1u) : cur;

            if(cur != ui.shift_menu_cursor)
            {
                if(ui.shift_menu_bootloader_armed)
                    CancelShiftBootloaderArm(ui);
                ui.shift_menu_cursor = cur;
                ui.ui_dirty = true;
            }
            return true;
        }
    }
    // EXT encoder click = select.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.shift_menu_cursor == ShiftSaveProject)
        {
            if(!project.metadata_scan_complete && !project.metadata_scan_requested)
            {
                const UiReq req{UiReqType::ScanProjectSlots, 0, 0};
                if(UiReq_Push(ui, worker, req))
                    project.metadata_scan_requested = true;
            }
            ui.save_project_menu_cursor = 0;
            ui.save_project_confirm_cursor = 1;
            if(UiNav_Push(ui.ui_nav, UiScreenId::SaveProjectMenu))
                ui.ui_dirty = true;
            return true;
        }
        if(ui.shift_menu_cursor == ShiftVolume)
        {
            return true;
        }
        if(ui.shift_menu_cursor == ShiftMicMonitor)
        {
            ui.settings_mic_monitor_enabled = !ui.settings_mic_monitor_enabled;
            ui.ui_dirty = true;
            return true;
        }
        if(ui.shift_menu_cursor == ShiftNormalize)
        {
            ui.settings_normalize_enabled = !ui.settings_normalize_enabled;
            if(ctx.shared)
                ctx.shared->settings_normalize_enabled.store(
                    ui.settings_normalize_enabled ? 1u : 0u, std::memory_order_release);
            ui.ui_dirty = true;
            return true;
        }
        if(ui.shift_menu_cursor == ShiftFirmwareUpdate)
        {
            ui.shift_menu_firmware_update_active = true;
            ui.ui_dirty = true;
            return true;
        }
        if(ui.shift_menu_cursor == ShiftMidiPanic)
        {
            Pod_MidiPanic();
            ui.ui_dirty = true;
            return true;
        }
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        if(ui.shift_menu_bootloader_armed)
        {
            CancelShiftBootloaderArm(ui);
            ui.ui_dirty = true;
            return true; // consume so BACK acts as cancel-first for bootloader arm
        }
    }

    if(e.type == UiInputType::BtnUp && e.id == kUiBtnExtEnc)
    {
        if(ui.shift_menu_bootloader_armed)
        {
            CancelShiftBootloaderArm(ui);
            ui.ui_dirty = true;
            return true;
        }
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        if(ui.shift_menu_bootloader_armed)
        {
            CancelShiftBootloaderArm(ui);
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
    OledPager& d = *ctx.display;
    d.Fill(false);

    const int screen_w = (int)d.Width();
    auto draw_centered_tiny = [&](const char* text, int y) {
        int x = (screen_w - TinyStringWidth(text)) / 2;
        if(x < 0)
            x = 0;
        DrawTinyString(d, text, x, y, true);
    };

    if(ui.shift_menu_bootloader_armed)
    {
        const uint32_t elapsed_ms = ctx.now_ms - ui.shift_menu_bootloader_arm_start_ms;
        if(elapsed_ms >= kShiftBootloaderHoldMs)
        {
            BeginShiftBootloaderLoading(ui, ctx.now_ms);
        }
        else if(!ui.shift_menu_firmware_update_active)
        {
            CancelShiftBootloaderArm(ui);
        }
        else
        {
            ui.ui_dirty = true;
        }
    }

    if(ui.shift_menu_bootloader_loading)
    {
        ui.ui_dirty = true;
        draw_centered_tiny("PAIRING - visit", 8);
        draw_centered_tiny("retroactivepedals", 22);
        draw_centered_tiny(".com to", 34);
        draw_centered_tiny("complete update", 48);

        const uint32_t loading_elapsed_ms = ctx.now_ms - ui.shift_menu_bootloader_loading_start_ms;
        if(loading_elapsed_ms >= kShiftBootloaderLoadingOverlayMs)
        {
            daisy::System::ResetToBootloader(daisy::System::DAISY_INFINITE_TIMEOUT);
        }
        return;
    }

    if(ui.shift_menu_firmware_update_active)
    {
        if(ui.shift_menu_bootloader_armed)
        {
            draw_centered_tiny("PAIRING - visit", 8);
            draw_centered_tiny("retroactivepedals", 22);
            draw_centered_tiny(".com to", 34);
            draw_centered_tiny("complete update", 48);
        }
        else
        {
            DrawTinyString(d, "FIRMWARE UPDATE", 10, 8, true);
            DrawTinyString(d, "HOLD down Right", 8, 24, true);
            DrawTinyString(d, "Encoder for two", 8, 34, true);
            DrawTinyString(d, "seconds to pair", 8, 44, true);
            DrawTinyString(d, "device", 8, 54, true);
        }
    }
    else
    {
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
        if(i == ShiftSaveProject)
        {
            if(sel) DrawFillOnlyTinyString(d, "SAVE PROJECT", label_x, label_y);
            else DrawTinyString(d, "SAVE PROJECT", label_x, label_y, true);
        }
        else if(i == ShiftVolume)
        {
            const char* label = "OUTPUT VOL";
            const int volume_label_x = label_x + 1;
            if(sel) DrawOutlineTinyString(d, label, volume_label_x, label_y);
            else DrawTinyString(d, label, volume_label_x, label_y, true);

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
        else if(i == ShiftMicMonitor)
        {
            const char* label = "MIC MONITOR";
            if(sel) DrawFillOnlyTinyString(d, label, label_x, label_y);
            else DrawTinyString(d, label, label_x, label_y, true);

            const char* value = ui.settings_mic_monitor_enabled ? "ON" : "OFF";
            const int val_w = TinyStringWidth(value);
            DrawTinyString(d, value, screen_w - val_w - 1, label_y, true);
        }
        else if(i == ShiftNormalize)
        {
            const char* label = "NORMALIZE";
            if(sel) DrawFillOnlyTinyString(d, label, label_x, label_y);
            else DrawTinyString(d, label, label_x, label_y, true);

            const char* value = ui.settings_normalize_enabled ? "ON" : "OFF";
            const int val_w = TinyStringWidth(value);
            DrawTinyString(d, value, screen_w - val_w - 1, label_y, true);
        }
        else if(i == ShiftFirmwareUpdate)
        {
            if(sel) DrawFillOnlyTinyString(d, "FIRMWARE UPDATE", label_x, label_y);
            else DrawTinyString(d, "FIRMWARE UPDATE", label_x, label_y, true);
        }
        else if(i == ShiftMidiPanic)
        {
            if(sel) DrawFillOnlyTinyString(d, "MIDI PANIC", label_x, label_y);
            else DrawTinyString(d, "MIDI PANIC", label_x, label_y, true);
        }
    }
    }

}
