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
constexpr uint32_t kCpuRecentWindowMs = 500u;

uint32_t s_cpu_recent_window_max_callback = 0u;
uint32_t s_cpu_displayed_now_callback = 0u;
uint32_t s_cpu_recent_window_max_cycles[kDiagAudioBucketCount]{};
uint32_t s_cpu_displayed_now_cycles[kDiagAudioBucketCount]{};
uint32_t s_voice_recent_window_max_cycles[kDiagVoiceBucketCount]{};
uint32_t s_voice_displayed_now_cycles[kDiagVoiceBucketCount]{};
uint32_t s_cpu_recent_window_start_ms = 0u;

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

uint32_t CyclesToPct_(uint32_t cycles, uint32_t budget_cycles)
{
    if(budget_cycles == 0u)
        return 0u;

    uint32_t pct = static_cast<uint32_t>(
        ((static_cast<uint64_t>(cycles) * 100u) + (budget_cycles / 2u)) / budget_cycles);
    if(pct > 999u)
        pct = 999u;
    return pct;
}

void ResetCpuRecentState_()
{
    s_cpu_recent_window_max_callback = 0u;
    s_cpu_displayed_now_callback = 0u;
    for(uint8_t i = 0; i < kDiagAudioBucketCount; ++i)
    {
        s_cpu_recent_window_max_cycles[i] = 0u;
        s_cpu_displayed_now_cycles[i] = 0u;
    }
    for(uint8_t i = 0; i < kDiagVoiceBucketCount; ++i)
    {
        s_voice_recent_window_max_cycles[i] = 0u;
        s_voice_displayed_now_cycles[i] = 0u;
    }
    s_cpu_recent_window_start_ms = 0u;
}

void UpdateCpuRecentState_(const AppDiagnosticsState& diag, uint32_t now_ms)
{
    if(s_cpu_recent_window_start_ms == 0u)
        s_cpu_recent_window_start_ms = now_ms;

    const uint32_t callback_last = diag.audio_cycles_last.load(std::memory_order_relaxed);
    if(callback_last > s_cpu_recent_window_max_callback)
        s_cpu_recent_window_max_callback = callback_last;

    for(uint8_t i = 0; i < kDiagAudioBucketCount; ++i)
    {
        const uint32_t cycles = diag.audio_bucket_cycles_last[i].load(std::memory_order_relaxed);
        if(cycles > s_cpu_recent_window_max_cycles[i])
            s_cpu_recent_window_max_cycles[i] = cycles;
    }
    for(uint8_t i = 0; i < kDiagVoiceBucketCount; ++i)
    {
        const uint32_t cycles = diag.voice_bucket_cycles_last[i].load(std::memory_order_relaxed);
        if(cycles > s_voice_recent_window_max_cycles[i])
            s_voice_recent_window_max_cycles[i] = cycles;
    }

    if((now_ms - s_cpu_recent_window_start_ms) < kCpuRecentWindowMs)
        return;

    s_cpu_displayed_now_callback = s_cpu_recent_window_max_callback;
    s_cpu_recent_window_max_callback = 0u;
    for(uint8_t i = 0; i < kDiagAudioBucketCount; ++i)
    {
        s_cpu_displayed_now_cycles[i] = s_cpu_recent_window_max_cycles[i];
        s_cpu_recent_window_max_cycles[i] = 0u;
    }
    for(uint8_t i = 0; i < kDiagVoiceBucketCount; ++i)
    {
        s_voice_displayed_now_cycles[i] = s_voice_recent_window_max_cycles[i];
        s_voice_recent_window_max_cycles[i] = 0u;
    }
    s_cpu_recent_window_start_ms = now_ms;
}

uint32_t LoadBucketNowPercent_(DiagAudioBucket bucket, uint32_t budget_cycles)
{
    const uint8_t index = static_cast<uint8_t>(bucket);
    return CyclesToPct_(s_cpu_displayed_now_cycles[index], budget_cycles);
}

uint32_t LoadBucketPeakPercent_(const AppDiagnosticsState& diag,
                                DiagAudioBucket            bucket,
                                uint32_t                   budget_cycles)
{
    const uint8_t index = static_cast<uint8_t>(bucket);
    const uint32_t cycles
        = diag.audio_bucket_cycles_peak[index].load(std::memory_order_relaxed);
    return CyclesToPct_(cycles, budget_cycles);
}

uint32_t LoadVoiceBucketNowPercent_(DiagVoiceBucket bucket, uint32_t budget_cycles)
{
    const uint8_t index = static_cast<uint8_t>(bucket);
    return CyclesToPct_(s_voice_displayed_now_cycles[index], budget_cycles);
}

uint32_t LoadVoiceBucketPeakPercent_(const AppDiagnosticsState& diag,
                                     DiagVoiceBucket            bucket,
                                     uint32_t                   budget_cycles)
{
    const uint8_t index = static_cast<uint8_t>(bucket);
    const uint32_t cycles
        = diag.voice_bucket_cycles_peak[index].load(std::memory_order_relaxed);
    return CyclesToPct_(cycles, budget_cycles);
}

void FormatNowPeakValue(char buf[16], uint32_t now_pct, uint32_t peak_pct)
{
    std::snprintf(buf,
                  16,
                  "%lu/%lu",
                  static_cast<unsigned long>(now_pct),
                  static_cast<unsigned long>(peak_pct));
}

void FormatBucketNowPeakValue(char                    buf[16],
                              const AppDiagnosticsState& diag,
                              DiagAudioBucket            bucket,
                              uint32_t                   budget_cycles)
{
    FormatNowPeakValue(buf,
                       LoadBucketNowPercent_(bucket, budget_cycles),
                       LoadBucketPeakPercent_(diag, bucket, budget_cycles));
}

void FormatVoiceBucketNowPeakValue(char                    buf[16],
                                   const AppDiagnosticsState& diag,
                                   DiagVoiceBucket            bucket,
                                   uint32_t                   budget_cycles)
{
    FormatNowPeakValue(buf,
                       LoadVoiceBucketNowPercent_(bucket, budget_cycles),
                       LoadVoiceBucketPeakPercent_(diag, bucket, budget_cycles));
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
            case UiReqType::LoadWavIndexSdManage: return "load sample";
            case UiReqType::DeleteWavIndex: return "delete sample";
            case UiReqType::NormalizeCurrent: return "normalize";
            case UiReqType::LoopFindCurrent: return "find loop";
            case UiReqType::SaveRenderedWavCurrent: return "render sample";
            case UiReqType::SaveRenderedWavNamed: return "render sample";
            case UiReqType::SaveSdManageTrimNamed: return "trim sample";
            case UiReqType::ReplaceSdManageTrimCurrent: return "trim sample";
            case UiReqType::SaveProject: return "save project";
            case UiReqType::LoadProject: return "load project";
            case UiReqType::ScanProjectSlots: return "scan projects";
            case UiReqType::DeleteProject: return "delete project";
            case UiReqType::RenameProject: return "rename project";
            case UiReqType::RenameWavIndex: return "rename sample";
            case UiReqType::UpdateWavStyleIndex: return "style sample";
            case UiReqType::UpdateProjectStyle: return "style project";
            default: return "working";
        }
    }

    if(worker_state.ui_req_result < 0)
        return "error";

    return "idle";
}
} // namespace

void UiOverlay_Update(UiOverlayState& o, const AppDiagnosticsState& diag, uint32_t now_ms)
{
    const bool want = o.modal_active;
    if(want && !o.visible)
    {
        o.shown_since_ms = now_ms;
        ResetCpuRecentState_();
        s_cpu_recent_window_start_ms = now_ms;
    }
    o.visible = want;

    if(o.visible)
        UpdateCpuRecentState_(diag, now_ms);
}

void UiOverlay_ResetCpuRecent()
{
    ResetCpuRecentState_();
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
    const int gain_a = static_cast<int>(p.engine_gain_db[0]);
    const int gain_b = static_cast<int>(p.engine_gain_db[1]);
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
        case kDiagOverlayPageCpuA:
        {
            FormatNowPeakValue(value0,
                               CyclesToPct_(s_cpu_displayed_now_callback, budget_cycles),
                               CyclesToPct_(diag.audio_cycles_peak.load(std::memory_order_relaxed),
                                            budget_cycles));
            FormatBucketNowPeakValue(
                value1, diag, kDiagAudioBucketCallbackPreVoice, budget_cycles);
            FormatBucketNowPeakValue(
                value2, diag, kDiagAudioBucketVoiceRender, budget_cycles);
            FormatBucketNowPeakValue(
                value3, diag, kDiagAudioBucketFxTotal, budget_cycles);
            FormatUnsignedValue(value4, diag.audio_late_count.load(std::memory_order_relaxed));
            const uint32_t voices_now = diag.voices_active.load(std::memory_order_relaxed);
            FormatUnsignedValue(value5, voices_now);
            // Marginal cost of one voice = total active-voice cycles / active count,
            // as a % of the audio budget. Directly shows how polyphony scales.
            uint32_t per_voice_cycles = 0u;
            if(voices_now > 0u)
                per_voice_cycles
                    = s_voice_displayed_now_cycles[kDiagVoiceBucketActiveVoicesTotal] / voices_now;
            FormatPercentValue(value6, CyclesToPct_(per_voice_cycles, budget_cycles));
            const OverlayMetric metrics[] = {
                {"callback pk", value0},
                {"pre-voice pk", value1},
                {"voice pk", value2},
                {"fx total pk", value3},
                {"late count", value4},
                {"active voices", value5},
                {"per-voice", value6},
            };
            DrawOverlayPage(oled, "cpu a", metrics, 7);
            return;
        }
        case kDiagOverlayPageCpuPre:
        {
            FormatBucketNowPeakValue(value0, diag, kDiagAudioBucketCallbackPreVoice, budget_cycles);
            FormatBucketNowPeakValue(value1, diag, kDiagAudioBucketPreParamsTick, budget_cycles);
            FormatBucketNowPeakValue(value2, diag, kDiagAudioBucketPreParamPush, budget_cycles);
            FormatBucketNowPeakValue(value3, diag, kDiagAudioBucketPreEvents, budget_cycles);
            const OverlayMetric metrics[] = {
                {"pre-voice pk", value0},
                {"params tick pk", value1},
                {"param push pk", value2},
                {"events pk", value3},
            };
            DrawOverlayPage(oled, "cpu pre", metrics, 4);
            return;
        }
        case kDiagOverlayPageCpuFx:
        {
            FormatBucketNowPeakValue(value0, diag, kDiagAudioBucketSat, budget_cycles);
            FormatBucketNowPeakValue(value1, diag, kDiagAudioBucketEq, budget_cycles);
            FormatBucketNowPeakValue(value2, diag, kDiagAudioBucketDelay, budget_cycles);
            FormatBucketNowPeakValue(value3, diag, kDiagAudioBucketReverb, budget_cycles);
            FormatBucketNowPeakValue(value4, diag, kDiagAudioBucketMaster, budget_cycles);
            FormatBucketNowPeakValue(
                value5, diag, kDiagAudioBucketRenderCapture, budget_cycles);
            FormatBucketNowPeakValue(value6, diag, kDiagAudioBucketMonitor, budget_cycles);
            const OverlayMetric metrics[] = {
                {"sat pk", value0},
                {"eq pk", value1},
                {"delay pk", value2},
                {"reverb pk", value3},
                {"master pk", value4},
                {"capture pk", value5},
                {"monitor pk", value6},
            };
            DrawOverlayPage(oled, "cpu fx", metrics, 7);
            return;
        }
        case kDiagOverlayPageVoiceCpu:
        {
            FormatVoiceBucketNowPeakValue(value0, diag, kDiagVoiceBucketRenderTotal, budget_cycles);
            FormatVoiceBucketNowPeakValue(
                value1, diag, kDiagVoiceBucketLayerEmphasis, budget_cycles);
            FormatVoiceBucketNowPeakValue(
                value2, diag, kDiagVoiceBucketActiveVoicesTotal, budget_cycles);
            FormatVoiceBucketNowPeakValue(value3, diag, kDiagVoiceBucketStealVoices, budget_cycles);
            FormatVoiceBucketNowPeakValue(value4, diag, kDiagVoiceBucketFetch, budget_cycles);
            FormatVoiceBucketNowPeakValue(value5, diag, kDiagVoiceBucketEnvMix, budget_cycles);
            FormatVoiceBucketNowPeakValue(value6, diag, kDiagVoiceBucketLayerMix, budget_cycles);
            const OverlayMetric metrics[] = {
                {"total pk", value0},
                {"emph pk", value1},
                {"active pk", value2},
                {"steal pk", value3},
                {"fetch pk", value4},
                {"envmix pk", value5},
                {"mix pk", value6},
            };
            DrawOverlayPage(oled, "voice cpu", metrics, 7);
            return;
        }
        case kDiagOverlayPageVoiceCpu2:
        {
            // Fetch-loop split: total fetch vs the loop-seam crossfade branch
            // (cycles %) and how many samples per block took that branch (raw
            // count, summed across voices). Non-seam read cost = fetch - seam.
            FormatVoiceBucketNowPeakValue(value0, diag, kDiagVoiceBucketFetch, budget_cycles);
            FormatVoiceBucketNowPeakValue(
                value1, diag, kDiagVoiceBucketFetchSeamCycles, budget_cycles);
            FormatUnsignedValue(value2, s_voice_displayed_now_cycles[kDiagVoiceBucketFetchSeamCount]);
            FormatUnsignedValue(value3,
                                diag.voice_bucket_cycles_peak[kDiagVoiceBucketFetchSeamCount].load(
                                    std::memory_order_relaxed));
            FormatVoiceBucketNowPeakValue(value4, diag, kDiagVoiceBucketVoiceSetup, budget_cycles);
            FormatVoiceBucketNowPeakValue(value5, diag, kDiagVoiceBucketEnvPresim, budget_cycles);
            const OverlayMetric metrics[] = {
                {"fetch pk", value0},
                {"seam cyc pk", value1},
                {"seam cnt now", value2},
                {"seam cnt pk", value3},
                {"setup pk", value4},
                {"presim pk", value5},
            };
            DrawOverlayPage(oled, "voice cpu2", metrics, 6);
            return;
        }
        case kDiagOverlayPageActivity:
        {
            FormatUnsignedValue(value0, diag.events_pushed.load(std::memory_order_relaxed));
            FormatPercentValue(value2, worker_pct);
            FormatSignedValue(value3, tune_a);
            FormatGainValue(value4, gain_a);
            FormatUnsignedValue(value5, diag.voice_steals.load(std::memory_order_relaxed));
            FormatUnsignedValue(value6, diag.voices_active.load(std::memory_order_relaxed));
            const OverlayMetric metrics[] = {
                {"events queued", value0},
                {"worker", worker_value},
                {"worker progress", value2},
                {"layer a tune", value3},
                {"layer a gain", value4},
                {"voice steals", value5},
                {"voices active", value6},
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
