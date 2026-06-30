#include "ui_screen_perform_process_internal.h"

#include "ui_screens_internal.h"

#include "app_state_engine.h"
#include "oled_pager.h"
#include "params.h"
#include "tilt_eq.h"

#include <cmath>
#include <cstdio>

void DrawProcessVolumeKnob(OledPager& d,
                           int cx,
                           int cy,
                           int radius,
                           const char* label,
                           float angle_rad,
                           bool focused);
void DrawProcessFilterKnob(OledPager& d,
                           int cx,
                           int cy,
                           int radius,
                           const char* label,
                           float angle_rad,
                           bool focused);
void DrawProcessFilterKnobInvertFocus(OledPager& d,
                                      int cx,
                                      int cy,
                                      int radius,
                                      const char* label,
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

float ClampEqShelfGain(float x)
{
    if(x < -kEqShelfGainMaxDb)
        return -kEqShelfGainMaxDb;
    if(x > kEqShelfGainMaxDb)
        return kEqShelfGainMaxDb;
    return x;
}

float ClampEqFilterQ(float x)
{
    if(x < kEqFilterQMin)
        return kEqFilterQMin;
    if(x > kEqFilterQMax)
        return kEqFilterQMax;
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

    // Global process filter knob angles (log cutoff 20 Hz..20 kHz; linear res).
    float fc = t.process_cutoff_hz;
    if(fc < 20.0f) fc = 20.0f;
    if(fc > 20000.0f) fc = 20000.0f;
    float cnorm = std::log(fc / 20.0f) / std::log(1000.0f); // log(20000/20)=log(1000)
    if(cnorm < 0.0f) cnorm = 0.0f;
    if(cnorm > 1.0f) cnorm = 1.0f;
    ui.cutoff_angle = 2.0943951f + (cnorm * 5.2359878f);
    float rnorm = t.process_resonance;
    if(rnorm < 0.0f) rnorm = 0.0f;
    if(rnorm > 1.0f) rnorm = 1.0f;
    ui.res_angle = 2.0943951f + (rnorm * 5.2359878f);
    return ui;
}

void DrawProcessLayerVolumePane(OledPager& d,
                                const ProcessLayerVolumeUiState& ui,
                                uint8_t main_cursor,
                                uint8_t cutres_sel,
                                int left_y,
                                int left_h)
{
    constexpr int kLeftX = 0;
    constexpr int kLeftW = 60;
    const int cx_mid = kLeftX + (kLeftW / 2) - 1; // 29

    // Two knobs stacked vertically: volume on top, the combined cut/res knob below.
    // Cursor: 0=vol, 1=cut/res. The cut/res knob shows whichever param is selected
    // (cutres_sel: 0=cut, 1=res); REnc-click swaps. Its focus uses the inverted
    // label look. Knob hands track the live global-filter params.
    constexpr int kVolRadius  = 7;
    constexpr int kFiltRadius = 6;
    const int vol_cy = left_y + 13;
    const int filt_cy = left_y + left_h - 16;

    DrawProcessVolumeKnob(d, cx_mid, vol_cy, kVolRadius, "vol", ui.angle_rad[0], main_cursor == 0u);
    const bool res_sel = (cutres_sel != 0u);
    DrawProcessFilterKnobInvertFocus(d,
                                     cx_mid,
                                     filt_cy,
                                     kFiltRadius,
                                     res_sel ? "res" : "cut",
                                     res_sel ? ui.res_angle : ui.cutoff_angle,
                                     main_cursor == 1u);
}


// Combined Process-EQ display range. Wider than the tilt-only +/-9 because the
// shelves reach +/-12 and the HP/LP filters dive well below.
static constexpr float kEqGraphDbRange = 15.f;

static void ProcessEqGraphRecomputeCurve(int16_t* y,
                                         int plot_x0,
                                         int plot_x1,
                                         int plot_y0,
                                         int plot_y1,
                                         const PerformParamsTargets& t)
{
    constexpr float kPlotSr = 48000.f;
    constexpr float kFMin = 20.f;
    constexpr float kFMax = 20000.f;
    const float kLogSpan = std::log10(static_cast<double>(kFMax / kFMin));

    const float center_hz = TiltEq_CenterNormToHz(t.eq_center_norm);
    const float tilt = ClampEqTiltDb(t.eq_tilt_db);
    // Pass raw Q; CascadeMagnitudeDb clamps per mode (tilt 0.4..0.8, bell up to
    // kEqBellQMax). Pre-clamping with ClampEqQ here capped the graphed bell at 0.8.
    const float eq_q = t.eq_q;

    // Lo/hi band coefficients computed once; magnitude evaluated per pixel.
    TiltEqPeakingCoef lo_c{};
    bool lo_on = true;
    if(t.eq_lo_is_filter)
        TiltEq_SetHighpassCoef(lo_c, t.eq_lo_cutoff_hz, ClampEqFilterQ(t.eq_lo_q), kPlotSr);
    else if(std::fabs(t.eq_lo_gain_db) >= kEqShelfNeutralDb)
        TiltEq_SetLowShelfCoef(lo_c, t.eq_lo_cutoff_hz, ClampEqShelfGain(t.eq_lo_gain_db), kPlotSr);
    else
        lo_on = false;

    TiltEqPeakingCoef hi_c{};
    bool hi_on = true;
    if(t.eq_hi_is_filter)
        TiltEq_SetLowpassCoef(hi_c, t.eq_hi_cutoff_hz, ClampEqFilterQ(t.eq_hi_q), kPlotSr);
    else if(std::fabs(t.eq_hi_gain_db) >= kEqShelfNeutralDb)
        TiltEq_SetHighShelfCoef(hi_c, t.eq_hi_cutoff_hz, ClampEqShelfGain(t.eq_hi_gain_db), kPlotSr);
    else
        hi_on = false;

    for(int px = plot_x0; px <= plot_x1; ++px)
    {
        const float tn = (static_cast<float>(px - plot_x0)) / static_cast<float>(plot_x1 - plot_x0);
        const float f = kFMin * std::pow(10.f, tn * kLogSpan);
        float db = TiltEq_CascadeMagnitudeDb(center_hz, tilt, f, kPlotSr, eq_q, t.eq_tilt_is_bell);
        if(lo_on)
            db += TiltEq_PeakingMagnitudeDb(lo_c, f, kPlotSr);
        if(hi_on)
            db += TiltEq_PeakingMagnitudeDb(hi_c, f, kPlotSr);
        if(db > kEqGraphDbRange)
            db = kEqGraphDbRange;
        if(db < -kEqGraphDbRange)
            db = -kEqGraphDbRange;
        const float yn = static_cast<float>(plot_y0)
                         + (kEqGraphDbRange - db) / (2.f * kEqGraphDbRange)
                               * static_cast<float>(plot_y1 - plot_y0);
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

void DrawEqGraphScreen(OledPager& d, const PerformParamsTargets& t, uint32_t now_ms, uint8_t eq_band, bool rshift)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;

    d.Fill(false);

    const int plot_x0 = 2;
    const int plot_x1 = kDisplayW - 3;
    const int plot_y0 = 15; // lowered to clear the taller header focus box
    const int plot_y1 = kDisplayH - 11;

    // Header: lo (left) | tilt/bell (center) | hi (right). The focused band
    // (eq_band: 0=tilt, 1=hi, 2=lo) gets an inverted (filled) box with a dotted
    // border 1px outside it. hy nudged down so the dotted border clears the top.
    const int hy = 4;
    auto draw_band_label = [&](const char* s, int cx, bool focused, bool inverted) {
        const int w = TinyStringWidth(s);
        const int lx = cx - w / 2;
        if(focused && rshift)
        {
            // RShift held (Q / filter-mode editing): plain solid border, all bands.
            d.DrawRect(lx - 2, hy - 2, lx + w + 1, hy + Font5x7::H + 1, true, false);
            DrawTinyString(d, s, lx, hy, true);
        }
        else if(focused && inverted)
        {
            // Tilt only: inverted (filled) box + dotted border 1px outside.
            d.DrawRect(lx - 2, hy - 2, lx + w + 1, hy + Font5x7::H + 1, true, true);
            DrawDottedRect(d, lx - 4, hy - 4, lx + w + 3, hy + Font5x7::H + 3, true);
            DrawTinyString(d, s, lx, hy, false); // dark glyphs on the lit box
        }
        else if(focused)
        {
            // lo / hi: dotted border only (no inversion).
            DrawDottedRect(d, lx - 2, hy - 2, lx + w + 1, hy + Font5x7::H + 1, true);
            DrawTinyString(d, s, lx, hy, true);
        }
        else
            DrawTinyString(d, s, lx, hy, true);
    };
    draw_band_label("lo", plot_x0 + 8, eq_band == 2u, false);
    draw_band_label(t.eq_tilt_is_bell ? "bell" : "tilt", kDisplayW / 2, eq_band == 0u, true);
    draw_band_label("hi", plot_x1 - 8, eq_band == 1u, false);

    static float s_prev[12];
    static bool  s_prev_init = false;
    static int16_t s_y[128];
    const float cur[12] = {t.eq_center_norm, t.eq_tilt_db, t.eq_q,
                           t.eq_lo_gain_db, t.eq_lo_cutoff_hz, t.eq_lo_is_filter ? 1.f : 0.f,
                           t.eq_lo_q,       t.eq_hi_gain_db,   t.eq_hi_cutoff_hz,
                           t.eq_hi_is_filter ? 1.f : 0.f, t.eq_hi_q,
                           t.eq_tilt_is_bell ? 1.f : 0.f};
    bool recompute = !s_prev_init;
    for(int i = 0; i < 12; ++i)
        if(cur[i] != s_prev[i])
            recompute = true;
    (void)now_ms;
    if(recompute)
    {
        ProcessEqGraphRecomputeCurve(s_y, plot_x0, plot_x1, plot_y0, plot_y1, t);
        for(int i = 0; i < 12; ++i)
            s_prev[i] = cur[i];
        s_prev_init = true;
    }

    for(int px = plot_x0; px < plot_x1; ++px)
    {
        // Skip the flat run pinned to the plot floor (the HP/LP stopband clamped
        // to -kEqGraphDbRange) so it doesn't draw a line along the bottom.
        if(s_y[px] >= plot_y1 && s_y[px + 1] >= plot_y1)
            continue;
        d.DrawLine(px, s_y[px], px + 1, s_y[px + 1], true);
    }

    // Tiny font: micro glyph set has no reliable digits (narrow "1" vanishes in 5x7->4x6 crop).
    const int lab_y = kDisplayH - Font5x7::H;
    auto draw_hz_label = [&](const char* s, float hz)
    {
        const int x = ProcessEqGraphHzToX(hz, plot_x0, plot_x1);
        const int w = TinyStringWidth(s);
        int lx = x - (w + 1) / 2; // centered on the frequency (round half left)
        if(lx < plot_x0)                 // keep the 20 Hz label fully on-screen
            lx = plot_x0;
        if(lx + w - 1 > plot_x1)         // keep the 20 kHz label fully on-screen
            lx = plot_x1 - w + 1;
        DrawTinyString(d, s, lx, lab_y, true);
    };
    draw_hz_label("20", 20.f);
    draw_hz_label("200", 200.f);
    draw_hz_label("2k", 2000.f);
    draw_hz_label("20k", 20000.f);
}
