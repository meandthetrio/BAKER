#include "ui_screens_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "oled_pager.h"
#include "project_actions.h"
#include "ui_input.h"
#include "ui_layout.h"
#include "ui_requests.h"

#include <cstdio>
#include <cstring>

namespace
{
static constexpr uint8_t kPresetsVisibleRows = 7;
static constexpr uint8_t kProjectActionCount = 2;
static constexpr uint8_t kRenameCols = 9;
static constexpr uint8_t kRenameRows = 4;
static constexpr int kRenameNameX = 2;
static constexpr int kRenameNameY = 8;
static constexpr int kRenameSaveY = 3;
static constexpr int kRenameGridX = 2;
static constexpr int kRenameGridY = 22;
static constexpr int kRenameGridXPitch = 14;
static constexpr int kRenameGridYPitch = 10;
static const char kRenameGrid[kRenameRows][kRenameCols + 1] = {
    "abcdefghi",
    "jklmnopqr",
    "stuvwxyz0",
    "123456789",
};
static const char kRenameSaveLabel[] = "save";

uint8_t WrapCursor(uint8_t value, int delta, uint8_t count)
{
    int next = static_cast<int>(value) + delta;
    while(next < 0)
        next += count;
    while(next >= static_cast<int>(count))
        next -= count;
    return static_cast<uint8_t>(next);
}

void RenderPresetsList(UiScreenCtx& ctx)
{
    if(!ctx.project || !ctx.display)
        return;

    AppProjectState& project = *ctx.project;
    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t visible_rows = kPresetsVisibleRows;
    if(visible_rows == 0u)
        return;

    static constexpr int kRowPitch = Font5x7::H + 2;
    const int total_h = static_cast<int>(visible_rows) * Font5x7::H
                        + static_cast<int>(visible_rows - 1u) * 2;
    const int start_y = (static_cast<int>(d.Height()) - total_h) / 2;

    uint8_t top_row = 0;
    if(project.current_project_slot >= visible_rows)
        top_row = static_cast<uint8_t>(project.current_project_slot - (visible_rows - 1u));
    const uint8_t max_top = static_cast<uint8_t>(kProjectSlotCount - visible_rows);
    if(top_row > max_top)
        top_row = max_top;

    for(uint8_t row = 0; row < visible_rows; ++row)
    {
        const uint8_t slot = static_cast<uint8_t>(top_row + row);
        if(slot >= kProjectSlotCount)
            break;

        const int row_y = start_y + static_cast<int>(row) * kRowPitch;
        const bool focused = (slot == project.current_project_slot);
        const char* label = ProjectActions_DisplayName(project, slot);
        if(focused)
        {
            DrawRencFocusTinyString(d, label, 1, row_y);
            continue;
        }

        DrawTinyString(d, label, 1, row_y, true);
    }
}

char RenameGridChar(uint8_t row, uint8_t col)
{
    return kRenameGrid[row % kRenameRows][col % kRenameCols];
}

bool QueueRenameRequest(AppUiState& ui, AppProjectState& project, AppWorkerState& worker)
{
    project.pending_rename_slot = project.current_project_slot;
    std::snprintf(project.pending_rename_name,
                  sizeof(project.pending_rename_name),
                  "%s",
                  ui.project_rename_draft);

    const UiReq req{UiReqType::RenameProject, project.pending_rename_slot, 0};
    if(!UiReq_Push(ui, worker, req))
        return false;

    UiNav_Pop(ui.ui_nav);
    UiNav_Pop(ui.ui_nav);
    ui.ui_dirty = true;
    return true;
}

void BuildRenameDisplayText(const AppProjectState& project,
                            const AppUiState& ui,
                            char* out,
                            size_t out_n)
{
    if(!out || out_n == 0)
        return;

    if(ui.project_rename_length == 0u)
    {
        std::snprintf(out,
                      out_n,
                      "%s",
                      ProjectActions_DisplayName(project, project.current_project_slot));
        return;
    }

    std::snprintf(out, out_n, "%s", ui.project_rename_draft);
}
} // namespace

void Presets_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.worker)
        return;

    AppProjectState& project = *ctx.project;
    if(project.metadata_scan_complete || project.metadata_scan_requested)
        return;

    const UiReq req{UiReqType::ScanProjectSlots, 0, 0};
    if(UiReq_Push(*ctx.ui, *ctx.worker, req))
        project.metadata_scan_requested = true;
}

bool Presets_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.project || !ctx.worker)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        AppProjectState& project = *ctx.project;
        const int next = static_cast<int>(project.current_project_slot) + e.value;
        project.current_project_slot = ProjectActions_WrapSlot(next);
        ctx.ui->ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        ctx.ui->project_action_cursor = 0;
        if(UiNav_Push(ctx.ui->ui_nav, UiScreenId::ProjectActionMenu))
            ctx.ui->ui_dirty = true;
        return true;
    }

    return false;
}

void Presets_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.display)
        return;

    RenderPresetsList(ctx);
}

bool ProjectActionMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.project || !ctx.worker)
        return false;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    const uint8_t slot = project.current_project_slot;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.project_action_cursor = WrapCursor(ui.project_action_cursor, e.value, kProjectActionCount);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.project_action_cursor == 0u)
        {
            UiNav_Pop(ui.ui_nav);
            return ProjectActions_TriggerRequest(ui, project, *ctx.worker, UiReqType::LoadProject, slot);
        }

        if(project.slot_has_file[slot] && UiNav_Push(ui.ui_nav, UiScreenId::RenameProject))
        {
            ui.ui_dirty = true;
            return true;
        }
        return true;
    }

    return false;
}

void ProjectActionMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    OledPager& d = *ctx.display;

    RenderPresetsList(ctx);

    const char* name = ProjectActions_DisplayName(project, project.current_project_slot);
    const bool rename_enabled = project.slot_has_file[project.current_project_slot];

    const int overlay_x0 = 20;
    const int overlay_y0 = 12;
    const int overlay_x1 = 107;
    const int overlay_y1 = 51;
    d.DrawRect(overlay_x0, overlay_y0, overlay_x1, overlay_y1, false, true);
    d.DrawRect(overlay_x0, overlay_y0, overlay_x1, overlay_y1, true, false);

    DrawTinyString(d, name, overlay_x0 + 4, overlay_y0 + 4, true);
    if(ui.project_action_cursor == 0u)
        DrawRencFocusTinyString(d, "load", overlay_x0 + 8, overlay_y0 + 18);
    else
        DrawTinyString(d, "load", overlay_x0 + 8, overlay_y0 + 18, true);

    if(ui.project_action_cursor == 1u)
        DrawRencFocusTinyString(d, "rename", overlay_x0 + 8, overlay_y0 + 30);
    else
        DrawTinyString(d, "rename", overlay_x0 + 8, overlay_y0 + 30, true);

    if(!rename_enabled)
        DrawTinyString(d, "empty slot", overlay_x0 + 8, overlay_y0 + 40, true);
}

void RenameProject_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project)
        return;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    ui.ui_lshift_held = false;
    ui.ui_parent_preview_active = false;
    ui.ui_parent_preview_from_top = 0;
    ui.ui_parent_preview_mode = 0;
    ui.ui_parent_preview_origin_screen = UiScreenId::COUNT;
    ui.ui_parent_preview_origin_main_cursor = 0;
    ui.ui_parent_preview_origin_fx_cursor = 0;
    ui.ui_parent_preview_origin_process_detail = false;
    ui.ui_parent_preview_origin_process_eq_graph = false;
    std::snprintf(ui.project_rename_draft,
                  sizeof(ui.project_rename_draft),
                  "%s",
                  project.slot_names[project.current_project_slot]);
    ui.project_rename_length = static_cast<uint8_t>(std::strlen(ui.project_rename_draft));
    ui.project_rename_grid_col = 0;
    ui.project_rename_grid_row = 0;
    ui.project_rename_focus = ProjectRenameFocus::Grid;
    ui.ui_dirty = true;
}

bool RenameProject_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.project || !ctx.worker)
        return false;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;

    if(e.type == UiInputType::EncDelta && e.value != 0)
    {
        if(e.id == kUiEncPod)
        {
            if(ui.project_rename_focus == ProjectRenameFocus::Save)
                return true;
            ui.project_rename_grid_col = WrapCursor(ui.project_rename_grid_col, e.value, kRenameCols);
            ui.ui_dirty = true;
            return true;
        }
        if(e.id == kUiEncExt)
        {
            if(ui.project_rename_focus == ProjectRenameFocus::Save)
            {
                if(e.value > 0)
                {
                    ui.project_rename_focus = ProjectRenameFocus::Grid;
                    ui.project_rename_grid_row = 0;
                    ui.ui_dirty = true;
                }
                return true;
            }

            if(e.value < 0 && ui.project_rename_grid_row == 0u)
            {
                ui.project_rename_focus = ProjectRenameFocus::Save;
                ui.ui_dirty = true;
                return true;
            }
            ui.project_rename_grid_row = WrapCursor(ui.project_rename_grid_row, e.value, kRenameRows);
            ui.ui_dirty = true;
            return true;
        }
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.project_rename_focus == ProjectRenameFocus::Save)
            return QueueRenameRequest(ui, project, *ctx.worker);

        if(ui.project_rename_length + 1u >= sizeof(ui.project_rename_draft))
            return true;

        ui.project_rename_draft[ui.project_rename_length] =
            RenameGridChar(ui.project_rename_grid_row, ui.project_rename_grid_col);
        ++ui.project_rename_length;
        ui.project_rename_draft[ui.project_rename_length] = '\0';
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        if(ui.project_rename_length > 0u)
        {
            --ui.project_rename_length;
            ui.project_rename_draft[ui.project_rename_length] = '\0';
            ui.ui_dirty = true;
        }
        return true;
    }

    return false;
}

void RenameProject_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    OledPager& d = *ctx.display;
    d.Fill(false);

    char display_name[24];
    BuildRenameDisplayText(project, ui, display_name, sizeof(display_name));
    d.SetCursor(kRenameNameX, kRenameNameY);
    d.WriteString(display_name, Font_6x8, true);

    const int save_x = static_cast<int>(d.Width()) - TinyStringWidth(kRenameSaveLabel) - 2;
    if(ui.project_rename_focus == ProjectRenameFocus::Save)
        DrawRencFocusTinyString(d, kRenameSaveLabel, save_x, kRenameSaveY);
    else
        DrawTinyString(d, kRenameSaveLabel, save_x, kRenameSaveY, true);

    for(uint8_t row = 0; row < kRenameRows; ++row)
    {
        for(uint8_t col = 0; col < kRenameCols; ++col)
        {
            char label[2] = {RenameGridChar(row, col), '\0'};
            const int x = kRenameGridX + static_cast<int>(col) * kRenameGridXPitch;
            const int y = kRenameGridY + static_cast<int>(row) * kRenameGridYPitch;
            const bool focused = (ui.project_rename_focus == ProjectRenameFocus::Grid)
                                 && (row == ui.project_rename_grid_row)
                                 && (col == ui.project_rename_grid_col);
            if(focused)
            {
                DrawRencFocusTinyString(d, label, x, y);
            }
            else
            {
                DrawTinyString(d, label, x, y, true);
            }
        }
    }
}
