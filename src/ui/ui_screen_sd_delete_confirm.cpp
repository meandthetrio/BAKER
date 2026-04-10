#include "ui_screens_internal.h"

#include "app_state.h"
#include "ui_input.h"
#include "ui_list_menu.h"
#include "ui_value_edit.h"
#include "ui_layout.h"
#include "oled_pager.h"
#include "ui_requests.h"
#include "sd_browser_state.h"
#include "sample_edit.h"

#include <cstdio>
#include <cstring>

using namespace daisy;
bool SdDeleteConfirm_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return false;

    AppState& app = *ctx.app;
    SdBrowserState& sd = app.ui.sd;

    const uint16_t idx = app.ui.sd_delete_index;
    if(!app.ui.sd_delete_mode || idx >= sd.wav_count || sd.scan_in_progress)
    {
        SdBrowser_SetStatus(sd, "DEL ERR");
        UiNav_Pop(app.ui.ui_nav);
        app.ui.sd_delete_mode = false;
        app.ui.ui_dirty = true;
        return true;
    }

    UiReq del{UiReqType::DeleteWavIndex, idx, 0};
    UiReq scan{UiReqType::ScanSdWavs, 0, 0};
    (void)UiReq_Push(app, del);
    (void)UiReq_Push(app, scan);

    sd.scan_in_progress = true;
    sd.scan_done = false;
    SdBrowser_SetStatus(sd, "DELETING");

    app.ui.sd_delete_mode = false;
    UiNav_Pop(app.ui.ui_nav);
    app.ui.ui_dirty = true;
    return true;
}

void SdDeleteConfirm_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    UiDraw_Header(d, layout, "DELETE WAV", status);

    d.SetCursor(layout.x, layout.y_body);
    char namebuf[32];
    if(app.ui.sd_delete_name[0] != '\0')
        std::snprintf(namebuf, sizeof(namebuf), "%s", app.ui.sd_delete_name);
    else
        std::snprintf(namebuf, sizeof(namebuf), "(no file)");
    d.WriteString(namebuf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    d.WriteString("ARE YOU SURE?", Font_6x8, true);
    d.SetCursor(layout.x, layout.y_body + layout.line_h * 3);
    d.WriteString("R:YES   L:NO", Font_6x8, true);
}
