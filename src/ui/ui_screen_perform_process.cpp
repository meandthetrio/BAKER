#include "ui_screens_internal.h"

#include <cmath>
#include <cstdio>

#include "app_state.h"
#include "oled_pager.h"
#include "params.h"
#include "tilt_eq.h"
#include "ui_input.h"
#include "ui_layout.h"

static float Clamp01(float x)
{
    if(x < 0.0f) return 0.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

static float UiAccelFromDtMs(uint32_t dt_ms)
{
    if(dt_ms <= 20u) return 8.0f;
    if(dt_ms <= 40u) return 5.0f;
    if(dt_ms <= 70u) return 3.0f;
    if(dt_ms <= 120u) return 2.0f;
    return 1.0f;
}

static float UiDeltaNormAccelerated(int enc_delta,
                                    uint32_t t_ms,
                                    uint32_t& last_t_ms,
                                    float base_step)
{
    const uint32_t dt_ms = (last_t_ms == 0u) ? 999u : (t_ms - last_t_ms);
    last_t_ms = t_ms;
    return static_cast<float>(enc_delta) * base_step * UiAccelFromDtMs(dt_ms);
}

static float ClampEqTiltDb(float x)
{
    if(x < -kTiltEqTiltMaxDb)
        return -kTiltEqTiltMaxDb;
    if(x > kTiltEqTiltMaxDb)
        return kTiltEqTiltMaxDb;
    return x;
}

static float ClampEqQ(float x)
{
    if(x < kTiltEqQMin)
        return kTiltEqQMin;
    if(x > kTiltEqQMax)
        return kTiltEqQMax;
    return x;
}

static constexpr float kProcessLayerLevelUiMax = 2.0f; // Match existing Daisy PROCESS edit clamp.

static void FormatProcessLevelDb(float level, char* out, size_t out_n)
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

static float ProcessLevelToKnobNorm(float level)
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

struct ProcessLayerVolumeUiState
{
    char  value_text[2][12];
    float angle_rad[2];
};

// Rendering-heavy PROCESS draw helpers live in ui_screen_perform_process_draw.cpp.
void DrawProcessKnob(OledPager& d,
                     int cx,
                     int cy,
                     int radius,
                     char side_letter,
                     const char* value_text,
                     float angle_rad,
                     bool focused);
void DrawProcessFxReorderOverlay(OledPager& d,
                                 int fader_x,
                                 int fader_y,
                                 int fader_w,
                                 int fader_h,
                                 int32_t selected_index,
                                 bool rshift_held);
void DrawFxDetailScreen(OledPager& d,
                        const PerformParamsTargets& t,
                        uint8_t fx_id,
                        uint8_t selected_param,
                        uint32_t now_ms,
                        bool rshift_held);

static uint8_t ProcessDetailParamCount(uint8_t fx_id)
{
    switch(fx_id)
    {
        case 0: return 4; // SAT + mode toggle
        case 1: return 1; // EQ: graph only (no classic detail)
        case 2: return 4; // DELAY: LTM RTM FBK MIX
        case 3: return 5; // REVERB
        default: return 3;
    }
}

static void ProcessHandleLayerToggle(UiScreenCtx& ctx)
{
    AppState& app = *ctx.app;
    app.perform_layer ^= 1u;
    const uint8_t layer = app.perform_layer & 1u;
    app.sd_current_slot.store(layer, std::memory_order_release);
    PublishEngineLayerParams(ctx);
    app.ui_dirty = true;
}

static void ProcessHandleLayerMuteToggle(UiScreenCtx& ctx, uint8_t layer)
{
    AppState& app = *ctx.app;
    PerformParamsTargets& t = ctx.params->EditTargets();
    if(ctx.rshift)
    {
        // RSHIFT + click => snap to UNITY.
        t.engine_layer_master_level[layer] = 1.0f;
        app.perform_process_vol_unmuted_level[layer] = 1.0f;
        app.perform_process_vol_muted[layer] = false;
        app.perform_process_vol_pct[layer] = 100u;
    }
    else
    {
        // Click => mute toggle for selected voice.
        if(!app.perform_process_vol_muted[layer])
        {
            float saved = t.engine_layer_master_level[layer];
            if(saved < 0.001f)
                saved = 1.0f;
            app.perform_process_vol_unmuted_level[layer] = saved;
            app.perform_process_vol_muted[layer] = true;
            t.engine_layer_master_level[layer] = 0.0f;
            app.perform_process_vol_pct[layer] = 0u;
        }
        else
        {
            float restore = app.perform_process_vol_unmuted_level[layer];
            if(restore < 0.0f)
                restore = 0.0f;
            if(restore > kProcessLayerLevelUiMax)
                restore = kProcessLayerLevelUiMax;
            app.perform_process_vol_muted[layer] = false;
            t.engine_layer_master_level[layer] = restore;
            app.perform_process_vol_pct[layer] = static_cast<uint16_t>(restore * 100.0f + 0.5f);
        }
    }
    ctx.params->PublishTargets();
    app.ui_dirty = true;
}

static void ProcessHandleLayerVolumeEdit(UiScreenCtx& ctx, const UiInputEvent& e, uint8_t layer)
{
    AppState& app = *ctx.app;
    if(app.perform_process_vol_muted[layer])
    {
        // Do not unmute on encoder turn; mute state toggles only on R-click.
        app.ui_dirty = true;
        return;
    }

    PerformParamsTargets& t = ctx.params->EditTargets();

    // Match SETTINGS volume acceleration + range exactly.
    static uint32_t s_last_t_ms = 0u;
    const uint32_t now_ms = e.t_ms;
    const uint32_t dt_ms = (s_last_t_ms == 0u) ? 999u : (now_ms - s_last_t_ms);
    s_last_t_ms = now_ms;

    float accel = 1.0f;
    if(dt_ms <= 25u)
        accel = 10.0f;
    else if(dt_ms <= 50u)
        accel = 6.0f;
    else if(dt_ms <= 90u)
        accel = 3.0f;
    else if(dt_ms <= 140u)
        accel = 2.0f;

    const float base_step = 0.01f;
    float next = t.engine_layer_master_level[layer] + static_cast<float>(e.value) * base_step * accel;
    if(next < 0.0f)
        next = 0.0f;
    if(next > kProcessLayerLevelUiMax)
        next = kProcessLayerLevelUiMax;
    t.engine_layer_master_level[layer] = next;
    app.perform_process_vol_unmuted_level[layer] = next;
    app.perform_process_vol_pct[layer] = static_cast<uint16_t>(next * 100.0f + 0.5f);
    ctx.params->PublishTargets();
    app.ui_dirty = true;
}

static void ProcessEditEqGraph(UiScreenCtx& ctx, float delta)
{
    PerformParamsTargets& t = ctx.params->EditTargets();
    if(ctx.rshift)
    {
        // RShift + Ext: Q for both bells (0.5 .. 1.7)
        t.eq_q = ClampEqQ(t.eq_q + delta * (kTiltEqQMax - kTiltEqQMin) * 1.25f);
    }
    else
    {
        t.eq_tilt_db = ClampEqTiltDb(t.eq_tilt_db + delta * 18.0f);
    }
}

static bool ProcessEditSatDetail(PerformParamsTargets& t,
                                 const UiInputEvent& e,
                                 uint8_t pidx,
                                 float delta)
{
    if(pidx == 0)
    {
        if(t.sat_mode == 0)
        {
            t.sat_drive = Clamp01(t.sat_drive + delta);
        }
        else
        {
            // ADSR behavior: RESO is a discrete 3-state selector.
            const int dir = (e.value > 0) ? 1 : -1;
            int idx = 0;
            if(t.sat_bit_reso >= 0.75f)
                idx = 2;
            else if(t.sat_bit_reso >= 0.25f)
                idx = 1;
            idx += dir;
            if(idx < 0) idx = 0;
            if(idx > 2) idx = 2;
            if(idx == 0) t.sat_bit_reso = 0.0f;      // CRUSH
            if(idx == 1) t.sat_bit_reso = 0.5f;      // STATIC
            if(idx == 2) t.sat_bit_reso = 1.0f;      // HISS
        }
        return true;
    }

    if(pidx == 1)
    {
        if(t.sat_mode == 0)
            t.sat_bump = Clamp01(t.sat_bump + delta);
        else
            t.sat_bit_smpl = Clamp01(t.sat_bit_smpl + delta);
        return true;
    }

    if(pidx == 2)
    {
        t.sat_mix = Clamp01(t.sat_mix + delta);
        t.sat_on = (t.sat_mix > 0.001f);
        return true;
    }

    if(pidx == 3)
    {
        const int dir = (e.value > 0) ? 1 : -1;
        int steps = (e.value > 0) ? e.value : -e.value;
        while(steps-- > 0)
            t.sat_mode = (dir > 0) ? ((t.sat_mode + 1u) & 1u) : ((t.sat_mode == 0) ? 1u : 0u);
        return true;
    }

    return false;
}

static bool ProcessEditDelayDetail(UiScreenCtx& ctx, PerformParamsTargets& t, uint8_t pidx, float delta)
{
    if(pidx == 0)
    {
        if(ctx.rshift)
        {
            t.delay_time_l = Clamp01(t.delay_time_l + delta);
            t.delay_time_r = Clamp01(t.delay_time_r + delta);
        }
        else
        {
            t.delay_time_l = Clamp01(t.delay_time_l + delta);
        }
        return true;
    }

    if(pidx == 1)
    {
        if(ctx.rshift)
        {
            t.delay_time_l = Clamp01(t.delay_time_l + delta);
            t.delay_time_r = Clamp01(t.delay_time_r + delta);
        }
        else
        {
            t.delay_time_r = Clamp01(t.delay_time_r + delta);
        }
        return true;
    }

    if(pidx == 2)
    {
        t.delay_feedback = Clamp01(t.delay_feedback + delta);
        return true;
    }

    if(pidx == 3)
    {
        t.delay_mix = Clamp01(t.delay_mix + delta);
        t.delay_on = (t.delay_mix > 0.001f);
        return true;
    }

    return false;
}

static bool ProcessEditReverbDetail(PerformParamsTargets& t, uint8_t pidx, float delta)
{
    if(pidx == 0)
    {
        t.reverb_pre = Clamp01(t.reverb_pre + delta);
        return true;
    }

    if(pidx == 1)
    {
        t.reverb_damp = Clamp01(t.reverb_damp + delta);
        return true;
    }

    if(pidx == 2)
    {
        t.reverb_decay = Clamp01(t.reverb_decay + delta);
        return true;
    }

    if(pidx == 3)
    {
        t.reverb_mod = Clamp01(t.reverb_mod + delta);
        return true;
    }

    if(pidx == 4)
    {
        t.reverb_mix = Clamp01(t.reverb_mix + delta);
        t.reverb_on = (t.reverb_mix > 0.001f);
        return true;
    }

    return false;
}

static bool ProcessEditFxDetail(UiScreenCtx& ctx,
                                const UiInputEvent& e,
                                uint8_t fx_id,
                                uint8_t pidx,
                                float delta)
{
    PerformParamsTargets& t = ctx.params->EditTargets();
    switch(fx_id)
    {
        case 0: return ProcessEditSatDetail(t, e, pidx, delta);
        case 2: return ProcessEditDelayDetail(ctx, t, pidx, delta);
        case 3:
        default: return ProcessEditReverbDetail(t, pidx, delta);
    }
}

static ProcessLayerVolumeUiState ProcessSyncLayerVolumeUiState(AppState& app,
                                                               const PerformParamsTargets& t)
{
    ProcessLayerVolumeUiState ui = {};
    for(int i = 0; i < 2; ++i)
    {
        app.perform_process_vol_pct[i]
            = static_cast<uint16_t>(t.engine_layer_master_level[i] * 100.0f + 0.5f);
        FormatProcessLevelDb(t.engine_layer_master_level[i], ui.value_text[i], sizeof(ui.value_text[i]));
        const float norm = ProcessLevelToKnobNorm(t.engine_layer_master_level[i]);
        ui.angle_rad[i] = 2.0943951f + (norm * 5.2359878f);
    }
    return ui;
}

static void DrawProcessLayerVolumePane(OledPager& d,
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

static void DrawEqGraphScreen(OledPager& d, const PerformParamsTargets& t, uint32_t now_ms)
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

bool PerformProcess_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app || !ctx.params)
        return false;
    if(ctx.shift)
        return false;

    AppState& app = *ctx.app;
    const uint8_t main_cursor = static_cast<uint8_t>(app.perform_process_main_cursor % 6u);
    const bool main_selects_fx = (main_cursor >= 2u);
    const uint8_t cursor = main_selects_fx ? static_cast<uint8_t>((main_cursor - 2u) & 0x03u)
                                           : static_cast<uint8_t>(app.perform_process_fx_cursor & 0x03u);
    const uint8_t fx_id = app.perform_process_fx_order[cursor];

    // POD2 toggles layer (same behavior as other PERFORM pages).
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        ProcessHandleLayerToggle(ctx);
        return true;
    }

    if(e.type == UiInputType::BtnDown
       && (e.id == kUiBtnPod1 || e.id == kUiBtnPodEnc)
       && (app.perform_process_detail_active || app.perform_process_eq_graph_active))
    {
        app.perform_process_detail_active   = false;
        app.perform_process_eq_graph_active = false;
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(app.perform_process_eq_graph_active)
        {
            app.ui_dirty = true;
            return true;
        }

        if(!app.perform_process_detail_active)
        {
            if(!main_selects_fx)
            {
                ProcessHandleLayerMuteToggle(ctx, main_cursor & 1u); // 0=VOL A, 1=VOL B
                return true;
            }
            {
                const uint8_t c = static_cast<uint8_t>((app.perform_process_main_cursor - 2u) & 0x03u);
                const uint8_t fid = app.perform_process_fx_order[c];
                if(fid == 1u)
                {
                    app.perform_process_eq_graph_active = true;
                }
                else
                {
                    app.perform_process_detail_active = true;
                    if(fid == 2u && app.perform_process_detail_param[c] > 3u)
                        app.perform_process_detail_param[c] = 0u;
                }
            }
            app.ui_dirty = true;
            return true;
        }

        // In ADSR-style FX detail, toggles change via encoder scroll, not click.
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        if(app.perform_process_eq_graph_active && fx_id == 1u)
        {
            PerformParamsTargets& t = ctx.params->EditTargets();
            const float step = 0.018f * static_cast<float>(e.value);
            t.eq_center_norm = Clamp01(t.eq_center_norm + step);
            ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }
        if(app.perform_process_detail_active)
        {
            int idx = static_cast<int>(app.perform_process_detail_param[cursor]) + e.value;
            const int count = static_cast<int>(ProcessDetailParamCount(fx_id));
            while(idx < 0)
                idx += count;
            while(idx >= count)
                idx -= count;
            app.perform_process_detail_param[cursor] = static_cast<uint8_t>(idx);
            app.ui_dirty = true;
            return true;
        }
        int idx = static_cast<int>(main_cursor) + e.value;
        while(idx < 0)
            idx += 6;
        while(idx >= 6)
            idx -= 6;
        app.perform_process_main_cursor = static_cast<uint8_t>(idx);
        if(app.perform_process_main_cursor >= 2u)
            app.perform_process_fx_cursor = static_cast<uint8_t>((app.perform_process_main_cursor - 2u) & 0x03u);
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        static uint32_t s_last_ext_t_ms = 0u;
        const float delta = UiDeltaNormAccelerated(e.value, e.t_ms, s_last_ext_t_ms, 0.02f);
        if(app.perform_process_eq_graph_active && fx_id == 1u)
        {
            ProcessEditEqGraph(ctx, delta);
            ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }
        if(app.perform_process_detail_active)
        {
            const uint8_t pidx = app.perform_process_detail_param[cursor];
            const bool changed = ProcessEditFxDetail(ctx, e, fx_id, pidx, delta);
            if(changed)
                ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }

        if(ctx.rshift)
        {
            if(!main_selects_fx)
            {
                app.ui_dirty = true;
                return true;
            }
            // RSHIFT + R encoder reorders focused S/E/D/R lane, clamped at edges.
            const int dir = (e.value > 0) ? 1 : -1;
            const int from = static_cast<int>(main_cursor - 2u);
            const int to = from + dir;
            if(to < 0 || to > 3)
            {
                app.ui_dirty = true;
                return true;
            }
            const uint8_t tmp = app.perform_process_fx_order[from];
            app.perform_process_fx_order[from] = app.perform_process_fx_order[to];
            app.perform_process_fx_order[to] = tmp;
            app.perform_process_fx_cursor = static_cast<uint8_t>(to);
            app.perform_process_main_cursor = static_cast<uint8_t>(to + 2);

            PerformParamsTargets& t = ctx.params->EditTargets();
            for(int i = 0; i < 4; ++i)
                t.fx_order[i] = app.perform_process_fx_order[i];
            ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }

        if(!main_selects_fx)
        {
            ProcessHandleLayerVolumeEdit(ctx, e, main_cursor & 1u);
            return true;
        }

        PerformParamsTargets& t = ctx.params->EditTargets();

        switch(fx_id)
        {
            case 0: // S = saturation drive
                t.sat_drive = Clamp01(t.sat_drive + delta);
                t.sat_on = (t.sat_drive > 0.001f);
                break;
            case 1: // E = EQ wet
                t.eq_mix = Clamp01(t.eq_mix + delta);
                t.eq_on  = (t.eq_mix > 0.001f);
                break;
            case 2: // D = delay wet
                t.delay_mix = Clamp01(t.delay_mix + delta);
                t.delay_on = (t.delay_mix > 0.001f);
                break;
            case 3: // R = reverb wet
            default:
                t.reverb_mix = Clamp01(t.reverb_mix + delta);
                t.reverb_on = (t.reverb_mix > 0.001f);
                break;
        }
        for(int i = 0; i < 4; ++i)
            t.fx_order[i] = app.perform_process_fx_order[i];

        ctx.params->PublishTargets();
        app.ui_dirty = true;
        return true;
    }

    return false;
}

void PerformProcess_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display || !ctx.params)
        return;

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);

    OledPager& d = *ctx.display;
    d.Fill(false);

    if(app.perform_process_eq_graph_active)
    {
        const uint8_t cursor = app.perform_process_fx_cursor & 0x03u;
        const uint8_t g_fx = app.perform_process_fx_order[cursor];
        if(g_fx == 1u)
        {
            const PerformParamsTargets& tg = ctx.params->TargetsForUI();
            DrawEqGraphScreen(d, tg, ctx.now_ms);
            return;
        }
        app.perform_process_eq_graph_active = false;
    }

    if(app.perform_process_detail_active)
    {
        const uint8_t cursor = app.perform_process_fx_cursor & 0x03u;
        const uint8_t fx_id = app.perform_process_fx_order[cursor];
        const uint8_t pidx = app.perform_process_detail_param[cursor];
        const PerformParamsTargets& t = ctx.params->TargetsForUI();
        DrawFxDetailScreen(d, t, fx_id, pidx, ctx.now_ms, ctx.rshift);
        return;
    }

    const UiLayout layout = UiLayout_Default();
    const char* header_label = "process";
    const int header_w = MicroStringWidth(header_label);
    const int box_w = header_w + 4;
    const int header_box_h = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash = static_cast<int32_t>(app.engine_header_invert_until_ms - ctx.now_ms) > 0;
    if(header_invert_flash)
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, header_box_h - 1, false, true);
        d.DrawRect(box_x, 0, box_x + box_w - 1, header_box_h - 1, true, false);
        DrawMicroString(d, header_label, box_x + 2, 2, true);
    }
    else
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, header_box_h - 1, true, true);
        DrawMicroString(d, header_label, box_x + 2, 2, false);
    }

    const uint8_t main_cursor = static_cast<uint8_t>(app.perform_process_main_cursor % 6u);
    const int32_t selected_index = (main_cursor >= 2u) ? static_cast<int32_t>(main_cursor - 2u) : -1;
    const int box_y = layout.y_body;
    const int box_h = layout.y_footer - layout.y_body + layout.line_h;
    const PerformParamsTargets& t = ctx.params->TargetsForUI();

    if(box_h > 24)
    {
        const ProcessLayerVolumeUiState layer_volume_ui = ProcessSyncLayerVolumeUiState(app, t);
        DrawProcessLayerVolumePane(d, layer_volume_ui, main_cursor, box_y, box_h);
    }

    // Keep right half for FX faders.
    constexpr int kPaneX = 60;
    constexpr int kPaneW = 64;
    const char* labels[4] = {"S", "E", "D", "R"};
    float values[4] = {};
    for(int i = 0; i < 4; ++i)
    {
        const uint8_t fx_id = app.perform_process_fx_order[i];
        switch(fx_id)
        {
            case 0: labels[i] = "S"; values[i] = Clamp01(t.sat_drive); break;
            case 1: labels[i] = "E"; values[i] = Clamp01(t.eq_mix); break;
            case 2: labels[i] = "D"; values[i] = Clamp01(t.delay_mix); break;
            case 3:
            default:
                labels[i] = "R";
                values[i] = Clamp01(t.reverb_mix);
                break;
        }
    }

    const int fader_x = kPaneX;
    const int fader_w = kPaneW;
    const int fader_y = box_y + 1;
    const int fader_h = box_h - 2;
    if(fader_w > 4 && fader_h > 4)
    {
        DrawVerticalFadersInRect(d,
                                 fader_x,
                                 fader_y,
                                 fader_w,
                                 fader_h,
                                 labels,
                                 values,
                                 4,
                                 true,
                                 selected_index,
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 1,
                                 1,
                                 1);

        DrawProcessFxReorderOverlay(d, fader_x, fader_y, fader_w, fader_h, selected_index, ctx.rshift);
    }
}
