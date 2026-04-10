#include "ui_overlay.h"

#include "app_state.h"
#include "params.h"
#include "ui_layout.h"
#include "oled_pager.h"

#include <cstdio>
#include <cstdlib>
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

void UiOverlay_Render(const AppState& app,
                      const Params& params,
                      const UiLayout& layout,
                      OledPager& oled)
{
    const uint32_t peak_cycles   = app.diag.audio_cycles_peak.load(std::memory_order_relaxed);
    const uint32_t budget_cycles = app.diag.audio_budget_cycles.load(std::memory_order_relaxed);
    uint32_t cpu_pct = 0;
    if(budget_cycles > 0)
        cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
    // Cap the display value so the compact fixed-width overlay format stays stable.
    if(cpu_pct > 999u)
        cpu_pct = 999u;

    const uint32_t late_cnt = app.diag.audio_late_count.load(std::memory_order_relaxed);
    const uint32_t clip_cnt = app.diag.clip_count.load(std::memory_order_relaxed);
    const uint32_t evq_ovf = app.diag.queue_overflows.load(std::memory_order_relaxed);
    const auto& p = params.current;
    const int tune_a = static_cast<int>(p.engine_tune_semitones[0]);
    const int tune_b = static_cast<int>(p.engine_tune_semitones[1]);
    const int gain_a = static_cast<int>(p.engine_gain_db[0]);
    const int gain_b = static_cast<int>(p.engine_gain_db[1]);
    const char* mode_a = p.engine_loop_mode[0] ? "LOOP" : "1SHOT";
    const char* mode_b = p.engine_loop_mode[1] ? "LOOP" : "1SHOT";

    // Mirror worker activity into compact overlay labels so diagnostics fit on one OLED row.
    const char* worker = "IDLE";
    if(app.worker.ui_req_busy)
    {
        switch(app.worker.ui_req_active)
        {
            case UiReqType::ScanSdWavs: worker = "SCAN"; break;
            case UiReqType::LoadWavIndex: worker = "LOAD"; break;
            case UiReqType::DeleteWavIndex: worker = "DEL"; break;
            case UiReqType::NormalizeCurrent: worker = "NORM"; break;
            case UiReqType::LoopFindCurrent: worker = "LOOPF"; break;
            case UiReqType::SaveRenderedWavCurrent: worker = "SAVE"; break;
            case UiReqType::SaveProject: worker = "PRJS"; break;
            case UiReqType::LoadProject: worker = "PRJL"; break;
            default: worker = "WORK"; break;
        }
    }
    else if(app.worker.ui_req_result < 0)
    {
        worker = "ERR";
    }
    const uint32_t worker_pct = app.worker.ui_req_busy ? app.worker.ui_req_progress : 0u;

    char buf[32];
    const int x = layout.x;
    const int y = layout.y_body;

    oled.SetCursor(x, y);
    std::snprintf(buf, sizeof(buf), "U:%02lu C:%04lu CPU:%03lu",
                  (unsigned long)app.ui.ui_hz,
                  (unsigned long)app.ui.ctrl_hz,
                  (unsigned long)cpu_pct);
    oled.WriteString(buf, Font_6x8, true);

    oled.SetCursor(x, y + layout.line_h);
    std::snprintf(buf, sizeof(buf), "LATE:%lu CLP:%lu EVQ:%lu",
                  (unsigned long)late_cnt,
                  (unsigned long)clip_cnt,
                  (unsigned long)evq_ovf);
    oled.WriteString(buf, Font_6x8, true);

    oled.SetCursor(x, y + layout.line_h * 2);
    std::snprintf(buf, sizeof(buf), "A T:%+d G:%+d.%dd %s", tune_a, gain_a / 10, std::abs(gain_a % 10), mode_a);
    oled.WriteString(buf, Font_6x8, true);

    oled.SetCursor(x, y + layout.line_h * 3);
    std::snprintf(buf, sizeof(buf), "B T:%+d G:%+d.%dd %s", tune_b, gain_b / 10, std::abs(gain_b % 10), mode_b);
    oled.WriteString(buf, Font_6x8, true);

    oled.SetCursor(x, y + layout.line_h * 4);
    std::snprintf(buf, sizeof(buf), "WK:%s %03lu E:%lu/%lu",
                  worker,
                  (unsigned long)worker_pct,
                  (unsigned long)app.diag.events_popped.load(std::memory_order_relaxed),
                  (unsigned long)app.diag.events_pushed.load(std::memory_order_relaxed));
    oled.WriteString(buf, Font_6x8, true);
}
