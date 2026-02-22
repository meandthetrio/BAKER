#include "ui_overlay.h"

#include "app_state.h"
#include "ui_layout.h"
#include "oled_pager.h"

#include <cstdio>
#include <cstring>

using namespace daisy;

void UiOverlay_Update(UiOverlayState& o, uint32_t now_ms, bool shift_held, bool editing)
{
    (void)editing;
    const bool want = shift_held;
    if(want && !o.visible)
        o.shown_since_ms = now_ms;
    o.visible = want;
}

void UiOverlay_Render(const AppState& app, const UiLayout& layout, OledPager& oled)
{
    const uint32_t peak_cycles   = app.audio_cycles_peak.load(std::memory_order_relaxed);
    const uint32_t budget_cycles = app.audio_budget_cycles.load(std::memory_order_relaxed);
    uint32_t cpu_pct = 0;
    if(budget_cycles > 0)
        cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
    if(cpu_pct > 999u)
        cpu_pct = 999u;

    const uint32_t late_cnt = app.audio_late_count.load(std::memory_order_relaxed);
    const uint32_t clip_cnt = app.clip_count.load(std::memory_order_relaxed);
    const uint32_t vact = app.voices_active.load(std::memory_order_relaxed);
    const uint32_t ovf_mod = app.ui_in_ovf % 1000;
    uint32_t hi = app.ui_in_hi;
    if(hi > 99u)
        hi = 99u;
    uint16_t rf = app.render_ms;
    uint16_t rhi = app.render_hi_ms;
    if(rf > 99u) rf = 99u;
    if(rhi > 99u) rhi = 99u;
    const uint32_t skip_mod = app.render_skips % 1000u;
    const char* sd_ok = app.sd.sd_ok ? "OK" : "ER";
    uint32_t wavs = app.sd.wav_count;
    if(wavs > 99u)
        wavs = 99u;
    const uint32_t ld = app.sd.load_in_progress ? app.sd.load_progress : 0;
    const uint32_t save_pct = app.sd.save_in_progress ? app.sd.save_progress : 0;
    const char* save_state = "IDLE";
    if(app.sd.save_in_progress)
        save_state = "BUSY";
    else if(app.sd.save_status[0] != '\0')
    {
        if(std::strncmp(app.sd.save_status, "SAVED", 5) == 0)
            save_state = "OK";
        else if(std::strncmp(app.sd.save_status, "SAVE ERR", 8) == 0)
            save_state = "ERR";
        else
            save_state = app.sd.save_status;
    }

    char buf[32];
    const int x = layout.x;
    const int y = layout.y_body;

    oled.SetCursor(x, y);
    std::snprintf(buf, sizeof(buf), "U:%02lu C:%04lu CPU:%03lu",
                  (unsigned long)app.ui_hz,
                  (unsigned long)app.ctrl_hz,
                  (unsigned long)cpu_pct);
    oled.WriteString(buf, Font_6x8, true);

    oled.SetCursor(x, y + layout.line_h);
    std::snprintf(buf, sizeof(buf), "LATE:%lu CLP:%lu V:%02lu",
                  (unsigned long)late_cnt,
                  (unsigned long)clip_cnt,
                  (unsigned long)vact);
    oled.WriteString(buf, Font_6x8, true);

    oled.SetCursor(x, y + layout.line_h * 2);
    std::snprintf(buf, sizeof(buf), "QO:%03lu H:%02lu R:%02u/%02u",
                  (unsigned long)ovf_mod,
                  (unsigned long)hi,
                  (unsigned)rf,
                  (unsigned)rhi);
    oled.WriteString(buf, Font_6x8, true);

    oled.SetCursor(x, y + layout.line_h * 3);
    std::snprintf(buf, sizeof(buf), "SD:%s W%02lu L%03lu S%03lu",
                  sd_ok,
                  (unsigned long)wavs,
                  (unsigned long)ld,
                  (unsigned long)skip_mod);
    oled.WriteString(buf, Font_6x8, true);

    oled.SetCursor(x, y + layout.line_h * 4);
    std::snprintf(buf, sizeof(buf), "SAVE:%s %03lu",
                  save_state,
                  (unsigned long)save_pct);
    oled.WriteString(buf, Font_6x8, true);
}
