#include "ui_screens_internal.h"

#include "app_state.h"
#include "ui_input.h"
#include "ui_layout.h"
#include "oled_pager.h"

#include <cstdio>
#include <cstring>

using namespace daisy;

static const char* ProjectActionLabel(ProjectAction action)
{
    switch(action)
    {
        case ProjectAction::Save: return "SAVE";
        case ProjectAction::Load: return "LOAD";
        default: return "NONE";
    }
}

static void ProjectStatusDisplayText(const AppState& app, char* out, size_t n)
{
    if(!out || n == 0)
        return;

    const char* msg = app.project_status;
    if(msg[0] == '\0')
    {
        std::snprintf(out, n, "%s", "WAITING");
        return;
    }

    const char* token = std::strchr(msg, ' ');
    token = (token && token[1] != '\0') ? (token + 1) : msg;
    if(std::strcmp(token, "ERR") == 0)
        std::snprintf(out, n, "%s", "ERROR");
    else
        std::snprintf(out, n, "%.*s", static_cast<int>(n - 1), token);
}

bool ProjectStatus_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(UiNav_Pop(ctx.app->ui_nav))
            ctx.app->ui_dirty = true;
        return true;
    }

    return false;
}

void ProjectStatus_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const AppState& app = *ctx.app;
    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status_text[12];
    ProjectStatusDisplayText(app, status_text, sizeof(status_text));
    UiDraw_Header(d, layout, "PROJECT", status_text);

    char buf[24];
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf,
                  sizeof(buf),
                  "PROJECT SLOT %02u",
                  static_cast<unsigned>(app.project_action_slot + 1u));
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    std::snprintf(buf, sizeof(buf), "ACTION: %s", ProjectActionLabel(app.project_action));
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    d.WriteString("STATUS:", Font_6x8, true);

    const int scale = 2;
    const int text_w = static_cast<int>(std::strlen(status_text)) * Font_6x8.FontWidth * scale;
    const int text_x = (128 - text_w) / 2;
    const int text_y = layout.y_body + layout.line_h * 3 + 2;
    DrawScaledText6x8(d, status_text, text_x, text_y, scale);

    UiDraw_Footer(d, layout, "RENC:EXIT");
}
