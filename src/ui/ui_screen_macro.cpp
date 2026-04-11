#include "ui_screens_internal.h"

#include <cstdio>

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "macros.h"
#include "oled_pager.h"
#include "ui_input.h"
#include "ui_layout.h"

static float Clamp01(float x)
{
    if(x < 0.0f) return 0.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

bool Macro_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;

    bool changed = false;
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod)
    {
        const uint8_t sel = ctx.shared->performance.macros.macro_ui.selected % kNumMacros;
        float v = ctx.shared->performance.macros.macro_ui.value[sel];
        v = Clamp01(v + (float)e.value * 0.02f);
        ctx.shared->performance.macros.macro_ui.value[sel] = v;
        Macros_Publish(*ctx.shared, ctx.shared->performance.macros.macro_ui);
        changed = true;
    }

    if(changed)
    {
        ctx.ui->ui_dirty = true;
        return true;
    }

    return false;
}

void Macro_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    const uint8_t sel = ctx.shared->performance.macros.macro_ui.selected % kNumMacros;
    uint32_t mac_val = (uint32_t)(ctx.shared->performance.macros.macro_ui.value[sel] * 100.0f + 0.5f);
    if(mac_val > 100)
        mac_val = 100;

    const char sel_char = (sel == 0) ? 'A' : 'B';

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(*ctx.shared, status, sizeof(status));
    UiDraw_Header(d, layout, "MAC", status);

    char buf[32];
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf, sizeof(buf), "MAC %c V:%03lu",
                  sel_char,
                  (unsigned long)mac_val);
    d.WriteString(buf, Font_6x8, true);

    UiDraw_Footer(d, layout, "POD:VAL P2:BACK");
}
