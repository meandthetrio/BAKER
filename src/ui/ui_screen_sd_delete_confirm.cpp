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
#include "ui_requests.h"
#include "sd_browser_state.h"

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

void BuildDeleteNameLine(const char* sample_name, char* out, size_t out_n, int max_w)
{
    if(!out || out_n == 0u)
        return;

    const char* visible_name = (sample_name && sample_name[0] != '\0') ? sample_name : "no file";
    std::snprintf(out, out_n, "%s", visible_name);
    if(TinyStringWidth(out) <= max_w)
        return;

    const size_t name_len = std::strlen(visible_name);
    for(size_t keep = name_len; keep > 0u; --keep)
    {
        std::snprintf(out,
                      out_n,
                      "%.*s...",
                      static_cast<int>(keep),
                      visible_name);
        if(TinyStringWidth(out) <= max_w)
            return;
    }

    std::snprintf(out, out_n, "...");
}

void CancelSampleDelete(AppUiState& ui)
{
    ui.sd_delete_mode = false;
    UiNav_Pop(ui.ui_nav);
    ui.ui_dirty = true;
}

bool ConfirmSampleDelete(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.worker)
        return false;

    AppUiState& ui = *ctx.ui;
    SdBrowserState& sd = ui.sd;
    const uint16_t idx = ui.sd_delete_index;
    if(!ui.sd_delete_mode || idx >= sd.wav_count || sd.scan_in_progress)
    {
        SdBrowser_SetStatus(sd, "DEL ERR");
        CancelSampleDelete(ui);
        return true;
    }

    UiReq del{UiReqType::DeleteWavIndex, idx, 0};
    if(!UiReq_Push(ui, *ctx.worker, del))
    {
        SdBrowser_SetStatus(sd, "DEL ERR");
        CancelSampleDelete(ui);
        return true;
    }

    SdBrowser_SetStatus(sd, "DELETING");
    ui.sd_delete_mode = false;
    UiNav_Pop(ui.ui_nav);
    if(UiNav_Active(ui.ui_nav) == UiScreenId::SdManageActionMenu)
        UiNav_Pop(ui.ui_nav);
    ui.ui_dirty = true;
    return true;
}
} // namespace

void SdDeleteConfirm_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;

    AppUiState& ui = *ctx.ui;
    SdBrowserState& sd = ui.sd;
    const uint16_t idx = ui.sd_delete_index;
    if(!ui.sd_delete_mode || idx >= sd.wav_count || sd.scan_in_progress)
    {
        SdBrowser_SetStatus(sd, "DEL ERR");
        ui.sd_delete_mode = false;
        UiNav_Pop(ui.ui_nav);
        ui.ui_dirty = true;
        return;
    }

    ui.sd_delete_confirm_cursor = 1u;
}

bool SdDeleteConfirm_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;

    AppUiState& ui = *ctx.ui;

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        CancelSampleDelete(ui);
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.sd_delete_confirm_cursor = WrapCursor(ui.sd_delete_confirm_cursor, e.value, 2u);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.sd_delete_confirm_cursor == 0u)
            return ConfirmSampleDelete(ctx);

        CancelSampleDelete(ui);
        return true;
    }

    return false;
}

void SdDeleteConfirm_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    OledPager& d = *ctx.display;
    d.Fill(false);

    static const char kConfirmDeleteLabel[] = "Confirm delete";
    static const char kYesLabel[] = "yes";
    static const char kNoLabel[] = "no";

    char name_line[32];
    BuildDeleteNameLine(ui.sd_delete_name, name_line, sizeof(name_line), static_cast<int>(d.Width()) - 8);

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

    if(ui.sd_delete_confirm_cursor == 0u)
        DrawFillOnlyTinyString(d, kYesLabel, yes_x, buttons_y);
    else
        DrawTinyString(d, kYesLabel, yes_x, buttons_y, true);

    if(ui.sd_delete_confirm_cursor == 1u)
        DrawFillOnlyTinyString(d, kNoLabel, no_x, buttons_y);
    else
        DrawTinyString(d, kNoLabel, no_x, buttons_y, true);
}
