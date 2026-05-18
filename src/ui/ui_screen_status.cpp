#include "ui_screens_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "ui_input.h"
#include "ui_layout.h"
#include "oled_pager.h"
#include "project_actions.h"

#include <cstdio>
#include <cstring>

using namespace daisy;

static void ProjectStatusDisplayText(const AppProjectState& project, char* out, size_t n)
{
    if(!out || n == 0)
        return;

    const char* msg = project.project_status;
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

static bool ProjectStatusIsLoading(const char* s)
{
    return std::strcmp(s, "LOADING") == 0 || std::strcmp(s, "WAITING") == 0;
}

bool ProjectStatus_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(UiNav_Pop(ctx.ui->ui_nav))
            ctx.ui->ui_dirty = true;
        return true;
    }

    return false;
}

void ProjectStatus_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display || !ctx.project)
        return;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    OledPager& d = *ctx.display;
    d.Fill(false);

    char status_text[16];
    ProjectStatusDisplayText(project, status_text, sizeof(status_text));
    const char* slot_name = ProjectActions_DisplayName(project, project.project_action_slot);
    const int title_x = (128 - TinyStringWidth(slot_name)) / 2;
    DrawTinyString(d, slot_name, title_x < 0 ? 0 : title_x, 2, true);

    bool is_loaded = (std::strcmp(status_text, "LOADED") == 0);
    if(project.project_action == ProjectAction::Load && is_loaded)
    {
        if(ui.project_status_loaded_since_ms == 0u)
            ui.project_status_loaded_since_ms = ctx.now_ms;
        else if((ctx.now_ms - ui.project_status_loaded_since_ms) >= 900u)
        {
            if(UiNav_Pop(ui.ui_nav))
                ui.ui_dirty = true;
        }
    }
    else
    {
        ui.project_status_loaded_since_ms = 0u;
    }

    char big[20];
    if(ProjectStatusIsLoading(status_text))
    {
        static const char* dots[] = {"   ", ".  ", ".. ", "..."};
        const uint8_t i = static_cast<uint8_t>((ctx.now_ms / 180u) & 0x3u);
        std::snprintf(big, sizeof(big), "LOADING%s", dots[i]);
    }
    else if(is_loaded)
    {
        std::snprintf(big, sizeof(big), "SUCCESS");
    }
    else
    {
        std::snprintf(big, sizeof(big), "%s", status_text);
    }

    const int scale = 2;
    const int text_w = static_cast<int>(std::strlen(big)) * Font_6x8.FontWidth * scale;
    const int text_x = (128 - text_w) / 2;
    const int text_y = 26;
    DrawScaledText6x8(d, big, text_x, text_y, scale);

    if(ProjectStatusIsLoading(status_text))
        ui.ui_dirty = true;
}
