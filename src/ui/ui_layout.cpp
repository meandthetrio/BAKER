#include "ui_layout.h"

#include "oled_pager.h"
#include "app_state_shared.h"
#include "app_state.h"

#include <cstdio>

using namespace daisy;
// Keep the global header text opt-in so screens own their visible status strings.
static constexpr bool kShowGlobalStatus = false;

UiLayout UiLayout_Default()
{
    UiLayout l{};
    l.x = 0;
    l.line_h = 8;
    l.y_header = 0;
    l.y_body = l.line_h;
    l.y_footer = 64 - l.line_h;
    l.rows_body = (l.y_footer - l.y_body) / l.line_h;
    return l;
}

void UiDraw_Header(OledPager& oled, const UiLayout& l, const char* title, const char* status)
{
    char buf[32];
    oled.SetCursor(l.x, l.y_header);
    if(status && status[0] != '\0')
        std::snprintf(buf, sizeof(buf), "%s %s", title ? title : "", status);
    else
        std::snprintf(buf, sizeof(buf), "%s", title ? title : "");
    oled.WriteString(buf, Font_6x8, true);
}

void UiDraw_Footer(OledPager& oled, const UiLayout& l, const char* hint)
{
    if(!hint)
        return;
    oled.SetCursor(l.x, l.y_footer);
    oled.WriteString(hint, Font_6x8, true);
}

void BuildStatus(const AppSharedState& shared, char* out, size_t n)
{
    if(!out || n == 0)
        return;
    if(!kShowGlobalStatus)
    {
        out[0] = '\0';
        return;
    }
    const char seq = shared.performance.seq_running ? '1' : '0';
    const char plk = shared.performance.plock_apply_enabled ? '1' : '0';
    const char* lfo = WaveChar(shared.performance.lfo_wave.load(std::memory_order_relaxed));
    std::snprintf(out, n, "S%c P%c L:%s", seq, plk, lfo);
}

void BuildStatus(const AppState& app, char* out, size_t n)
{
    BuildStatus(app.shared, out, n);
}
