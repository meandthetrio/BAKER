#include "ui_screen_perform_process_internal.h"

#include "ui_screens_internal.h"

#include "app_state_engine.h"
#include "oled_pager.h"
#include "params.h"
#include "tilt_eq.h"

#include <cmath>
#include <cstdio>

void DrawProcessKnob(OledPager& d,
                     int cx,
                     int cy,
                     int radius,
                     char side_letter,
                     const char* value_text,
                     float angle_rad,
                     bool focused);

float Clamp01(float x)
{
    if(x < 0.0f) return 0.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

float ClampEqTiltDb(float x)
{
    if(x < -kTiltEqTiltMaxDb)
        return -kTiltEqTiltMaxDb;
    if(x > kTiltEqTiltMaxDb)
        return kTiltEqTiltMaxDb;
    return x;
}

float ClampEqQ(float x)
{
    if(x < kTiltEqQMin)
        return kTiltEqQMin;
    if(x > kTiltEqQMax)
        return kTiltEqQMax;
    return x;
}

void FormatProcessLevelDb(float level, char* out, size_t out_n)
{
    if(out == nullptr || out_n == 0)
        return;

    if(level <= 0.00001f)
    {
        std::snprintf(out, out_n, "-infdb");
        return;
    }

    const float db = 20.0f * std::log10(level);
    if(db > 0.049f)
        std::snprintf(out, out_n, "+%.1fdb", static_cast<double>(db));
    else if(db < -0.049f)
        std::snprintf(out, out_n, "%.1fdb", static_cast<double>(db));
    else
        std::snprintf(out, out_n, "0.0db");
}

float ProcessLevelToKnobNorm(float level)
{
    if(level <= 0.00001f)
        return 0.0f;
    if(level <= 1.0f)
    {
        float db = 20.0f * std::log10(level);
        if(db < -60.0f)
            db = -60.0f;
        return ((db + 60.0f) / 60.0f) * 0.5f;
    }

    float db = 20.0f * std::log10(level);
    const float max_db = 20.0f * std::log10(kProcessLayerLevelUiMax);
    if(db > max_db)
        db = max_db;
    if(max_db <= 0.0f)
        return 0.5f;
    return 0.5f + (db / max_db) * 0.5f;
}

ProcessLayerVolumeUiState ProcessSyncLayerVolumeUiState(AppEngineState& engine,
                                                       const PerformParamsTargets& t)
{
    ProcessLayerVolumeUiState ui = {};
    for(int i = 0; i < 2; ++i)
    {
        engine.process.perform_process_vol_pct[i]
            = static_cast<uint16_t>(t.engine_layer_master_level[i] * 100.0f + 0.5f);
        FormatProcessLevelDb(t.engine_layer_master_level[i], ui.value_text[i], sizeof(ui.value_text[i]));
        const float norm = ProcessLevelToKnobNorm(t.engine_layer_master_level[i]);
        ui.angle_rad[i] = 2.0943951f + (norm * 5.2359878f);
    }
    return ui;
}

void DrawProcessLayerVolumePane(OledPager& d,
                                const ProcessLayerVolumeUiState& ui,
                                uint8_t main_cursor,
                                int left_y,
                                int left_h)
{
    constexpr int kLeftX = 0;
    constexpr int kLeftW = 60;
    constexpr int kVolKnobRadius = 9;

    const int knob_cx = kLeftX + (kLeftW / 2) - 1;
    const int a_cy = left_y + 13;
    const int b_cy = left_y + left_h - 13;
    DrawProcessKnob(
        d, knob_cx, a_cy, kVolKnobRadius, 'a', ui.value_text[0], ui.angle_rad[0], main_cursor == 0u);
    DrawProcessKnob(
        d, knob_cx, b_cy, kVolKnobRadius, 'b', ui.value_text[1], ui.angle_rad[1], main_cursor == 1u);
}

static bool ProcessEqGraphNeedsRecompute(uint32_t now_ms,
                                         float eq_center_norm,
                                         float eq_tilt_db,
                                         float eq_q,
                                         uint32_t last_curve_ms,
                                         float prev_c,
                                         float prev_t,
                                         float prev_q)
{
    return (now_ms - last_curve_ms >= 72u) || (std::fabs(eq_center_norm - prev_c) > 0.0005f)
           || (std::fabs(eq_tilt_db - prev_t) > 0.02f) || (std::fabs(eq_q - prev_q) > 0.015f);
}

static void ProcessEqGraphRecomputeCurve(int16_t* y,
                                         int plot_x0,
                                         int plot_x1,
                                         int plot_y0,
                                         int plot_y1,
                                         float center_hz,
                                         float tilt,
                                         float eq_q)
{
    constexpr float kPlotSr = 48000.f;
    constexpr float kFMin = 20.f;
    constexpr float kFMax = 20000.f;
    const float kLogSpan = std::log10(static_cast<double>(kFMax / kFMin));

    for(int px = plot_x0; px <= plot_x1; ++px)
    {
        const float tn = (static_cast<float>(px - plot_x0)) / static_cast<float>(plot_x1 - plot_x0);
        const float f = kFMin * std::pow(10.f, tn * kLogSpan);
        float db = TiltEq_CascadeMagnitudeDb(center_hz, tilt, f, kPlotSr, eq_q);
        if(db > 9.f)
            db = 9.f;
        if(db < -9.f)
            db = -9.f;
        const float yn
            = static_cast<float>(plot_y0) + (9.f - db) / 18.f * static_cast<float>(plot_y1 - plot_y0);
        int py = static_cast<int>(yn + 0.5f);
        if(py < plot_y0)
            py = plot_y0;
        if(py > plot_y1)
            py = plot_y1;
        y[px] = static_cast<int16_t>(py);
    }
}

static int ProcessEqGraphHzToX(float hz, int plot_x0, int plot_x1)
{
    constexpr float kFMin = 20.f;
    constexpr float kFMax = 20000.f;
    const float kLogSpan = std::log10(static_cast<double>(kFMax / kFMin));
    const float lg = static_cast<float>(std::log10(static_cast<double>(hz / kFMin))) / kLogSpan;
    return plot_x0 + static_cast<int>(lg * static_cast<float>(plot_x1 - plot_x0) + 0.5f);
}

void DrawEqGraphScreen(OledPager& d, const PerformParamsTargets& t, uint32_t now_ms)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;

    d.Fill(false);

    const int plot_x0 = 2;
    const int plot_x1 = kDisplayW - 3;
    const int plot_y0 = 9;
    const int plot_y1 = kDisplayH - 11;

    const char* hdr = "eq";
    const int hw = MicroStringWidth(hdr);
    int hx = (kDisplayW - hw) / 2;
    if(hx < 0)
        hx = 0;
    DrawMicroString(d, hdr, hx, 0, true);

    const float center_hz = TiltEq_CenterNormToHz(t.eq_center_norm);
    const float tilt = ClampEqTiltDb(t.eq_tilt_db);

    static uint32_t s_last_curve_ms = 0u;
    static float s_prev_c = -999.f;
    static float s_prev_t = -999.f;
    static float s_prev_q = -999.f;
    static int16_t s_y[128];

    const float eq_q = ClampEqQ(t.eq_q);
    const bool recompute = ProcessEqGraphNeedsRecompute(
        now_ms, t.eq_center_norm, t.eq_tilt_db, eq_q, s_last_curve_ms, s_prev_c, s_prev_t, s_prev_q);

    if(recompute)
    {
        ProcessEqGraphRecomputeCurve(s_y, plot_x0, plot_x1, plot_y0, plot_y1, center_hz, tilt, eq_q);
        s_last_curve_ms = now_ms;
        s_prev_c = t.eq_center_norm;
        s_prev_t = t.eq_tilt_db;
        s_prev_q = eq_q;
    }

    for(int px = plot_x0; px < plot_x1; ++px)
        d.DrawLine(px, s_y[px], px + 1, s_y[px + 1], true);

    // Tiny font: micro glyph set has no reliable digits (narrow "1" vanishes in 5x7->4x6 crop).
    const int lab_y = kDisplayH - Font5x7::H;
    auto draw_hz_label = [&](const char* s, float hz)
    {
        const int x = ProcessEqGraphHzToX(hz, plot_x0, plot_x1);
        const int w = TinyStringWidth(s);
        const int lx = x - w / 2;
        DrawTinyString(d, s, lx, lab_y, true);
    };
    draw_hz_label("100", 100.f);
    draw_hz_label("1k", 1000.f);
    draw_hz_label("10k", 10000.f);
}
