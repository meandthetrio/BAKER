#include "ui_overlay.h"

#include "app_state_diagnostics.h"
#include "app_state_ui.h"
#include "app_state_worker.h"
#include "params.h"
#include "ui_draw_text.h"
#include "ui_layout.h"
#include "oled_pager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace daisy;

namespace
{
struct OverlayMetric
{
    const char* label;
    const char* value;
};

constexpr int kOverlayTitleRowY  = 1;
constexpr int kOverlayMetricRows = 7;
constexpr int kOverlayRowH       = 8;
constexpr int kOverlayMetricX    = 0;

int FontStringWidth(const char* str)
{
    if(!str)
        return 0;
    return static_cast<int>(std::strlen(str)) * Font_6x8.FontWidth;
}

void DrawOverlayTitle(OledPager& oled, const char* title)
{
    const int title_w = MicroStringWidth(title);
    const int title_x = (static_cast<int>(oled.Width()) - title_w) / 2;
    DrawMicroString(oled, title, title_x, kOverlayTitleRowY, true);
}

void DrawOverlayMetricRow(OledPager& oled, int row, const char* label, const char* value)
{
    const int row_y   = (row + 1) * kOverlayRowH;
    const int label_y = row_y + 1;
    const int value_w = FontStringWidth(value);
    const int value_x = static_cast<int>(oled.Width()) - value_w;

    DrawMicroString(oled, label, kOverlayMetricX, label_y, true);
    oled.SetCursor(value_x, row_y);
    oled.WriteString(value, Font_6x8, true);
}

void DrawOverlayPage(OledPager& oled, const char* title, const OverlayMetric* metrics, int count)
{
    oled.Fill(false);
    DrawOverlayTitle(oled, title);
    for(int i = 0; i < count && i < kOverlayMetricRows; ++i)
        DrawOverlayMetricRow(oled, i, metrics[i].label, metrics[i].value);
}

void FormatUnsignedValue(char buf[16], uint32_t value)
{
    std::snprintf(buf, 16, "%lu", static_cast<unsigned long>(value));
}

void FormatPercentValue(char buf[16], uint32_t value)
{
    std::snprintf(buf, 16, "%lu%%", static_cast<unsigned long>(value));
}

void FormatSignedValue(char buf[16], int value)
{
    std::snprintf(buf, 16, "%+d", value);
}

void FormatGainValue(char buf[16], int deci_db)
{
    std::snprintf(buf, 16, "%+d.%01ddB", deci_db / 10, std::abs(deci_db % 10));
}

void FormatProbeDbValue(char buf[16], float value_db)
{
    const int rounded_db = static_cast<int>((value_db >= 0.0f) ? (value_db + 0.5f) : (value_db - 0.5f));
    std::snprintf(buf, 16, "%+d", rounded_db);
}

const char* LoopModeValue(bool loop_mode)
{
    return loop_mode ? "loop" : "single";
}

const char* WorkerStateValue(const AppWorkerState& worker_state)
{
    if(worker_state.ui_req_busy)
    {
        switch(worker_state.ui_req_active)
        {
            case UiReqType::ScanSdWavs: return "scan samples";
            case UiReqType::LoadWavIndex: return "load sample";
            case UiReqType::DeleteWavIndex: return "delete sample";
            case UiReqType::NormalizeCurrent: return "normalize";
            case UiReqType::LoopFindCurrent: return "find loop";
            case UiReqType::SaveRenderedWavCurrent: return "render sample";
            case UiReqType::SaveProject: return "save project";
            case UiReqType::LoadProject: return "load project";
            case UiReqType::ScanProjectSlots: return "scan projects";
            case UiReqType::RenameProject: return "rename project";
            case UiReqType::RenameWavIndex: return "rename sample";
            default: return "working";
        }
    }

    if(worker_state.ui_req_result < 0)
        return "error";

    return "idle";
}
} // namespace

void UiOverlay_Update(UiOverlayState& o, uint32_t now_ms)
{
    const bool want = o.modal_active;
    if(want && !o.visible)
        o.shown_since_ms = now_ms;
    o.visible = want;
}

void UiOverlay_Render(const AppUiState& ui,
                      const AppDiagnosticsState& diag,
                      const AppWorkerState& worker_state,
                      const Params& params,
                      const UiLayout& layout,
                      OledPager& oled)
{
    (void)layout;

    const uint32_t peak_cycles   = diag.audio_cycles_peak.load(std::memory_order_relaxed);
    const uint32_t budget_cycles = diag.audio_budget_cycles.load(std::memory_order_relaxed);
    uint32_t cpu_pct = 0;
    if(budget_cycles > 0)
        cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
    if(cpu_pct > 999u)
        cpu_pct = 999u;

    const auto& p = params.current;
    const int tune_a = static_cast<int>(p.engine_tune_semitones[0]);
    const int tune_b = static_cast<int>(p.engine_tune_semitones[1]);
    const int gain_a = static_cast<int>(p.engine_gain_db[0]);
    const int gain_b = static_cast<int>(p.engine_gain_db[1]);
    const char* mode_a = LoopModeValue(p.engine_loop_mode[0]);
    const char* mode_b = LoopModeValue(p.engine_loop_mode[1]);
    const char* worker_value = WorkerStateValue(worker_state);
    const uint32_t worker_pct = worker_state.ui_req_busy ? worker_state.ui_req_progress : 0u;

    char value0[16];
    char value1[16];
    char value2[16];
    char value3[16];
    char value4[16];
    char value5[16];
    char value6[16];

    switch(diag.overlay.page)
    {
        case kDiagOverlayPageActivity:
        {
            FormatUnsignedValue(value0, diag.events_pushed.load(std::memory_order_relaxed));
            FormatPercentValue(value2, worker_pct);
            FormatSignedValue(value3, tune_a);
            FormatGainValue(value4, gain_a);
            FormatSignedValue(value6, tune_b);
            const OverlayMetric metrics[] = {
                {"events queued", value0},
                {"worker", worker_value},
                {"worker progress", value2},
                {"layer a tune", value3},
                {"layer a gain", value4},
                {"layer a mode", mode_a},
                {"layer b tune", value6},
            };
            DrawOverlayPage(oled, "activity", metrics, 7);
            return;
        }
        case kDiagOverlayPageLayerAndMix:
        {
            FormatGainValue(value0, gain_b);
            FormatProbeDbValue(value2, diag.gain_probe_display_db[kDiagGainProbeAPre]);
            FormatProbeDbValue(value3, diag.gain_probe_display_db[kDiagGainProbeAPost]);
            FormatProbeDbValue(value4, diag.gain_probe_display_db[kDiagGainProbeBPre]);
            FormatProbeDbValue(value5, diag.gain_probe_display_db[kDiagGainProbeBPost]);
            FormatProbeDbValue(value6, diag.gain_probe_display_db[kDiagGainProbeSumPreFx]);
            const OverlayMetric metrics[] = {
                {"layer b gain", value0},
                {"layer b mode", mode_b},
                {"layer a pre", value2},
                {"layer a post", value3},
                {"layer b pre", value4},
                {"layer b post", value5},
                {"sum before effects", value6},
            };
            DrawOverlayPage(oled, "layer and mix", metrics, 7);
            return;
        }
        case kDiagOverlayPageOutputAndClamps:
        {
            FormatProbeDbValue(value0, diag.gain_probe_display_db[kDiagGainProbeFxPreMaster]);
            FormatProbeDbValue(value1, diag.gain_probe_display_db[kDiagGainProbeOutFinal]);
            FormatUnsignedValue(value2, diag.sat_softclip_hits.load(std::memory_order_relaxed));
            FormatUnsignedValue(value3, diag.master_softclip_hits.load(std::memory_order_relaxed));
            FormatUnsignedValue(value4, diag.monitor_clamp_hits.load(std::memory_order_relaxed));
            const OverlayMetric metrics[] = {
                {"effects before master", value0},
                {"final output", value1},
                {"sample softclip", value2},
                {"master softclip", value3},
                {"monitor clamps", value4},
            };
            DrawOverlayPage(oled, "output and clamps", metrics, 5);
            return;
        }
        case kDiagOverlayPageSys:
        default:
        {
            FormatUnsignedValue(value0, ui.ui_hz);
            FormatUnsignedValue(value1, ui.ctrl_hz);
            FormatPercentValue(value2, cpu_pct);
            FormatUnsignedValue(value3, diag.audio_late_count.load(std::memory_order_relaxed));
            FormatUnsignedValue(value4, diag.clip_count.load(std::memory_order_relaxed));
            FormatUnsignedValue(value5, diag.queue_overflows.load(std::memory_order_relaxed));
            FormatUnsignedValue(value6, diag.events_popped.load(std::memory_order_relaxed));
            const OverlayMetric metrics[] = {
                {"user interface", value0},
                {"control rate", value1},
                {"processor load", value2},
                {"late callbacks", value3},
                {"clip count", value4},
                {"event overflows", value5},
                {"events handled", value6},
            };
            DrawOverlayPage(oled, "system", metrics, 7);
            return;
        }
    }
}
