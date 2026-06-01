#include "ui_screens_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "ui_input.h"
#include "oled_pager.h"
#include "project_actions.h"
#include "ui_requests.h"

#include <cstdio>
#include <cstring>

using namespace daisy;

namespace
{
uint8_t WrapCursor(uint8_t value, int delta, uint8_t count)
{
    int next = static_cast<int>(value) + delta;
    while(next < 0)
        next += count;
    while(next >= static_cast<int>(count))
        next -= count;
    return static_cast<uint8_t>(next);
}

void DrawFillOnlyTinyString(OledPager& d, const char* str, int x, int y)
{
    if(!str)
        return;
    const int w = TinyStringWidth(str);
    d.DrawRect(x - 2, y - 2, x + w + 1, y + Font5x7::H + 1, true, true);
    DrawTinyString(d, str, x, y, false);
}

int CenterTinyX(OledPager& d, const char* str)
{
    if(!str)
        return 0;
    return (static_cast<int>(d.Width()) - TinyStringWidth(str)) / 2;
}

void BuildDeleteNameLine(const char* project_name, char* out, size_t out_n, int max_w)
{
    if(!out || out_n == 0u)
        return;

    const char* visible_name = (project_name && project_name[0] != '\0') ? project_name : "----";
    std::snprintf(out, out_n, "%s", visible_name);
    if(TinyStringWidth(out) <= max_w)
        return;

    const size_t name_len = std::strlen(visible_name);
    for(size_t keep = name_len; keep > 0u; --keep)
    {
        std::snprintf(out, out_n, "%.*s...", static_cast<int>(keep), visible_name);
        if(TinyStringWidth(out) <= max_w)
            return;
    }

    std::snprintf(out, out_n, "...");
}

void SetProjectDeleteStatusImmediate(AppProjectState& project, uint8_t slot, const char* msg)
{
    project.project_action = ProjectAction::Delete;
    project.project_action_slot = slot;
    std::snprintf(project.project_status,
                  sizeof(project.project_status),
                  "P%02u %s",
                  static_cast<unsigned>(slot + 1u),
                  msg ? msg : "");
}

void CancelProjectDelete(AppUiState& ui)
{
    ui.project_delete_mode = false;
    UiNav_Pop(ui.ui_nav);
    ui.ui_dirty = true;
}

bool ConfirmProjectDelete(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.worker)
        return false;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    const uint8_t slot = ui.project_delete_slot;
    if(!ui.project_delete_mode || slot >= kProjectSlotCount || !project.slot_has_file[slot])
    {
        SetProjectDeleteStatusImmediate(project, slot, "ERR");
        CancelProjectDelete(ui);
        return true;
    }

    SetProjectDeleteStatusImmediate(project, slot, "DELETING");
    const UiReq del{UiReqType::DeleteProject, slot, 0};
    if(!UiReq_Push(ui, *ctx.worker, del))
    {
        SetProjectDeleteStatusImmediate(project, slot, "ERR");
        ui.project_delete_mode = false;
        UiNav_Pop(ui.ui_nav);
        if(UiNav_Active(ui.ui_nav) == UiScreenId::ProjectActionMenu)
            UiNav_Pop(ui.ui_nav);
        UiNav_Push(ui.ui_nav, UiScreenId::ProjectStatus);
        ui.ui_dirty = true;
        return true;
    }

    ui.project_delete_pending = true;
    ui.project_delete_pending_slot = slot;
    ui.project_delete_pending_done_count = ctx.worker->ui_req_done_count;
    ui.project_delete_mode = false;
    UiNav_Pop(ui.ui_nav);
    if(UiNav_Active(ui.ui_nav) == UiScreenId::ProjectActionMenu)
        UiNav_Pop(ui.ui_nav);
    ui.ui_dirty = true;
    return true;
}
} // namespace

void ProjectDeleteConfirm_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project)
        return;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    const uint8_t slot = ui.project_delete_slot;
    if(!ui.project_delete_mode || slot >= kProjectSlotCount || !project.slot_has_file[slot])
    {
        SetProjectDeleteStatusImmediate(project, slot, "ERR");
        ui.project_delete_mode = false;
        UiNav_Pop(ui.ui_nav);
        ui.ui_dirty = true;
        return;
    }

    ui.project_delete_confirm_cursor = 1u;
}

bool ProjectDeleteConfirm_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;

    AppUiState& ui = *ctx.ui;

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        CancelProjectDelete(ui);
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.project_delete_confirm_cursor = WrapCursor(ui.project_delete_confirm_cursor, e.value, 2u);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.project_delete_confirm_cursor == 0u)
            return ConfirmProjectDelete(ctx);

        CancelProjectDelete(ui);
        return true;
    }

    return false;
}

void ProjectDeleteConfirm_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    OledPager& d = *ctx.display;
    d.Fill(false);

    static const char kConfirmDeleteLabel[] = "Confirm delete";
    static const char kYesLabel[] = "yes";
    static const char kNoLabel[] = "no";

    char name_line[32];
    BuildDeleteNameLine(ProjectActions_DisplayName(project, ui.project_delete_slot),
                        name_line,
                        sizeof(name_line),
                        static_cast<int>(d.Width()) - 8);

    const int line_gap = 6;
    const int button_gap = 12;
    const int block_h = (Font5x7::H * 3) + (line_gap * 2);
    const int top_y = (static_cast<int>(d.Height()) - block_h) / 2;
    const int confirm_y = top_y;
    const int name_y = confirm_y + Font5x7::H + line_gap;
    const int buttons_y = name_y + Font5x7::H + line_gap;

    const int yes_box_w = TinyStringWidth(kYesLabel) + 4;
    const int no_box_w = TinyStringWidth(kNoLabel) + 4;
    const int pair_w = yes_box_w + button_gap + no_box_w;
    const int pair_x0 = (static_cast<int>(d.Width()) - pair_w) / 2;
    const int yes_x = pair_x0 + 2;
    const int no_x = pair_x0 + yes_box_w + button_gap + 2;

    DrawTinyString(d, kConfirmDeleteLabel, CenterTinyX(d, kConfirmDeleteLabel), confirm_y, true);
    DrawTinyStringCaseSensitive(d, name_line, CenterTinyX(d, name_line), name_y, true);

    if(ui.project_delete_confirm_cursor == 0u)
        DrawFillOnlyTinyString(d, kYesLabel, yes_x, buttons_y);
    else
        DrawTinyString(d, kYesLabel, yes_x, buttons_y, true);

    if(ui.project_delete_confirm_cursor == 1u)
        DrawFillOnlyTinyString(d, kNoLabel, no_x, buttons_y);
    else
        DrawTinyString(d, kNoLabel, no_x, buttons_y, true);
}
