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

static void DrawFxDetailScreen(OledPager& d,
                               const PerformParamsTargets& t,
                               uint8_t fx_id,
                               uint8_t selected_param,
                               uint32_t now_ms,
                               bool rshift_held)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;
    constexpr int kPerformFaderCount = 4;
    constexpr int kDelayFaderCount = 4;
    constexpr int kReverbFaderCount = 5;
    constexpr int kBitResoStepCount = 3;
    static const char* kBitResoLabels[kBitResoStepCount] = {"CRUSH", "STATIC", "HISS"};
    auto bit_reso_index = [](float value) -> int
    {
        if(value < 0.0f) value = 0.0f;
        if(value > 1.0f) value = 1.0f;
        int idx = static_cast<int>(value * static_cast<float>(kBitResoStepCount - 1) + 0.5f);
        if(idx < 0) idx = 0;
        if(idx >= kBitResoStepCount) idx = kBitResoStepCount - 1;
        return idx;
    };

    const char* labels[kPerformFaderCount] = {"SATURATION", "EQ", "DELAY", "REVERB"};
    int index = static_cast<int>(fx_id & 0x03u);
    if(index < 0 || index >= kPerformFaderCount)
        index = 0;

    if(index == 2)
    {
        const char* hdr       = "delay";
        const int   header_w  = MicroStringWidth(hdr);
        const int   box_w     = header_w + 4;
        const int   header_box_h = kMicroH + 4;
        int         box_x     = kDisplayW - box_w;
        if(box_x < 0)
            box_x = 0;
        d.DrawRect(box_x, 0, box_x + box_w - 1, header_box_h - 1, true, true);
        DrawMicroString(d, hdr, box_x + 2, 2, false);
    }
    else
    {
        const char* label  = labels[index];
        const int   text_w = TinyStringWidth(label);
        int         text_x = (kDisplayW - text_w) / 2;
        if(text_x < 0)
            text_x = 0;
        DrawTinyString(d, label, text_x, 1, true);
    }

    if(index == 0)
    {
        constexpr int kMargin = 2;
        constexpr int kGap = 2;
        const int block_x = kMargin;
        const int block_w = kDisplayW / 4;
        const int block_y = Font5x7::H + 4;
        int block_h = kDisplayH - block_y - kMargin;
        if(block_h < 3) block_h = 3;
        const int box_h = (block_h - kGap) / 2;
        const bool tape_selected = (t.sat_mode == 0);
        const bool bit_selected = (t.sat_mode == 1);
        const bool mode_select_active = (selected_param == 3);
        if(mode_select_active)
            d.DrawRect(block_x - 1, block_y - 1, block_x + block_w, block_y + block_h, true, false);
        d.DrawRect(block_x, block_y, block_x + block_w - 1, block_y + box_h - 1, true, tape_selected);
        d.DrawRect(block_x,
                   block_y + box_h + kGap,
                   block_x + block_w - 1,
                   block_y + (box_h * 2) + kGap - 1,
                   true,
                   bit_selected);
        const int label_w1 = TinyStringWidth("TAPE");
        const int label_w2 = TinyStringWidth("BIT");
        const int label_y1 = block_y + (box_h - Font5x7::H) / 2;
        const int label_y2 = block_y + box_h + kGap + (box_h - Font5x7::H) / 2;
        int label_x1 = block_x + (block_w - label_w1) / 2;
        int label_x2 = block_x + (block_w - label_w2) / 2;
        if(label_x1 < block_x + 1) label_x1 = block_x + 1;
        if(label_x2 < block_x + 1) label_x2 = block_x + 1;
        DrawTinyString(d, "TAPE", label_x1, label_y1, !tape_selected);
        DrawTinyString(d, "BIT", label_x2, label_y2, !bit_selected);

        const int fader_offset = 8;
        const int fader_x = block_x + block_w + kGap + fader_offset;
        const int fader_w = kDisplayW - fader_x - kMargin;
        if(fader_w > 4)
        {
            const char* fader_labels[3] = {(t.sat_mode == 1) ? "RESO" : "SAT",
                                           (t.sat_mode == 1) ? "SMPL" : "BUMP",
                                           "MIX"};
            const float fader_values[3] = {(t.sat_mode == 1) ? t.sat_bit_reso : t.sat_drive,
                                           (t.sat_mode == 1) ? t.sat_bit_smpl : t.sat_bump,
                                           t.sat_mix};
            int param_index = selected_param;
            const bool fader_select_active = (param_index >= 0 && param_index < 3);
            if(!fader_select_active && !mode_select_active) param_index = 0;
            const int fader_offsets[3] = {0, 0, 0};
            const bool circle_handles[3] = {false, false, false};
            const bool hide_rails[3] = {t.sat_mode == 1, false, false};
            const bool hide_handles[3] = {t.sat_mode == 1, false, false};
            DrawVerticalFadersInRect(d,
                                     fader_x,
                                     block_y,
                                     fader_w,
                                     block_h,
                                     fader_labels,
                                     fader_values,
                                     3,
                                     fader_select_active,
                                     param_index,
                                     fader_offsets,
                                     circle_handles,
                                     hide_rails,
                                     hide_handles);
            if(t.sat_mode == 1)
            {
                const int label_y = block_y + block_h - Font5x7::H - 1;
                int line_top = block_y + 2;
                int line_bottom = label_y - 2;
                if(line_bottom > line_top)
                {
                    int fader_left = fader_x + 2;
                    int line_x = fader_left;
                    const char* lbl = "RESO";
                    const int lbl_w = TinyStringWidth(lbl);
                    int lbl_x = line_x - (lbl_w / 2);
                    if(lbl_x < fader_x + 1) lbl_x = fader_x + 1;
                    if(lbl_x + lbl_w > fader_x + fader_w - 2)
                        lbl_x = fader_x + fader_w - 2 - lbl_w;
                    line_x = lbl_x + (lbl_w / 2);
                    const int cur_idx = bit_reso_index(t.sat_bit_reso);
                    const int label_top = line_top + 1;
                    const int label_gap = 3;
                    int label_y0 = label_top;
                    for(int i = 0; i < kBitResoStepCount; ++i)
                    {
                        const char* bits_label = kBitResoLabels[i];
                        const int bits_w = TinyStringWidth(bits_label);
                        const int bits_x = line_x - (bits_w / 2);
                        const int bits_y = label_y0 + (i * (Font5x7::H + label_gap));
                        if(bits_y >= line_top && bits_y <= line_bottom - Font5x7::H)
                        {
                            const bool is_selected = (i == cur_idx);
                            if(is_selected)
                            {
                                d.DrawRect(bits_x - 1,
                                           bits_y - 1,
                                           bits_x + bits_w,
                                           bits_y + Font5x7::H,
                                           true,
                                           true);
                                DrawTinyString(d, bits_label, bits_x, bits_y, false);
                            }
                            else
                            {
                                DrawTinyString(d, bits_label, bits_x, bits_y, true);
                            }
                        }
                    }
                }
            }
        }
    }
    else if(index == 1)
    {
        // EQ uses the graph submenu (Ext click); classic detail is not used for fx_id 1.
        const char* msg = "EXT: graph";
        const int mw = TinyStringWidth(msg);
        int mx = (kDisplayW - mw) / 2;
        if(mx < 0)
            mx = 0;
        DrawTinyString(d, msg, mx, kDisplayH / 2 - Font5x7::H, true);
    }
    else if(index == 2)
    {
        constexpr int kMargin = 2;
        const int header_box_h = kMicroH + 4;
        const int block_y = header_box_h;
        int block_h = kDisplayH - block_y - kMargin;
        if(block_h < 3)
            block_h = 3;
        const int fader_x = kMargin;
        const int fader_w = kDisplayW - (kMargin * 2);
        if(fader_w > 4)
        {
            const char* fader_labels[kDelayFaderCount] = {"LTM", "RTM", "FBK", "MIX"};
            const float fader_values[kDelayFaderCount]
                = {t.delay_time_l, t.delay_time_r, t.delay_feedback, t.delay_mix};
            int param_index = selected_param;
            const bool fader_select_active = (param_index >= 0 && param_index < kDelayFaderCount);
            if(!fader_select_active)
                param_index = 0;
            int lb_x0[4], lb_y0[4], lb_x1[4], lb_y1[4];
            DrawDelayDetailFaders(d,
                                  fader_x,
                                  block_y,
                                  fader_w,
                                  block_h,
                                  fader_labels,
                                  fader_values,
                                  fader_select_active,
                                  param_index,
                                  lb_x0,
                                  lb_y0,
                                  lb_x1,
                                  lb_y1);

            if(rshift_held)
            {
                DrawClockwiseMarchingDottedRect(
                    d, lb_x0[0] - 1, lb_y0[0] - 1, lb_x1[0] + 1, lb_y1[0] + 1, now_ms);
                DrawClockwiseMarchingDottedRect(
                    d, lb_x0[1] - 1, lb_y0[1] - 1, lb_x1[1] + 1, lb_y1[1] + 1, now_ms);
            }
            else
            {
                if(fader_select_active && param_index == 0)
                    DrawDottedRect(d, lb_x0[0] - 1, lb_y0[0] - 1, lb_x1[0] + 1, lb_y1[0] + 1, true);
                if(fader_select_active && param_index == 1)
                    DrawDottedRect(d, lb_x0[1] - 1, lb_y0[1] - 1, lb_x1[1] + 1, lb_y1[1] + 1, true);
            }
        }
    }
    else if(index == 3)
    {
        constexpr int kMargin = 2;
        const int block_y = Font5x7::H + 4;
        int block_h = kDisplayH - block_y - kMargin;
        if(block_h < 3) block_h = 3;
        const int fader_x = kMargin;
        const int fader_w = kDisplayW - (kMargin * 2);
        if(fader_w > 4)
        {
            const char* fader_labels[kReverbFaderCount] = {"Pre", "Dmp", "Dcy", "Mod", "Wet"};
            const float fader_values[kReverbFaderCount]
                = {t.reverb_pre, t.reverb_damp, t.reverb_decay, t.reverb_mod, t.reverb_mix};
            int param_index = selected_param;
            const bool fader_select_active = (param_index >= 0 && param_index < kReverbFaderCount);
            if(!fader_select_active) param_index = 0;
            const bool hide_handles[kReverbFaderCount] = {false, false, false, false, false};
            const bool hide_rails[kReverbFaderCount] = {false, false, false, false, false};
            const int fader_offsets[kReverbFaderCount] = {0, 1, -1, 0, 0};
            DrawVerticalFadersInRect(d,
                                     fader_x,
                                     block_y,
                                     fader_w,
                                     block_h,
                                     fader_labels,
                                     fader_values,
                                     kReverbFaderCount,
                                     fader_select_active,
                                     param_index,
                                     fader_offsets,
                                     nullptr,
                                     hide_rails,
                                     hide_handles);
        }
    }
}

static void DrawEqGraphScreen(OledPager& d, const PerformParamsTargets& t, uint32_t now_ms)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;
    constexpr float kPlotSr = 48000.f;
    constexpr float kFMin = 20.f;
    constexpr float kFMax = 20000.f;

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
    const float kLogSpan = std::log10(static_cast<double>(kFMax / kFMin));

    static uint32_t s_last_curve_ms = 0u;
    static float s_prev_c = -999.f;
    static float s_prev_t = -999.f;
    static float s_prev_q = -999.f;
    static int16_t s_y[128];

    const float eq_q = ClampEqQ(t.eq_q);
    const bool recompute = (now_ms - s_last_curve_ms >= 72u)
                           || (std::fabs(t.eq_center_norm - s_prev_c) > 0.0005f)
                           || (std::fabs(t.eq_tilt_db - s_prev_t) > 0.02f)
                           || (std::fabs(eq_q - s_prev_q) > 0.015f);

    if(recompute)
    {
        for(int px = plot_x0; px <= plot_x1; ++px)
        {
            const float tn = (static_cast<float>(px - plot_x0))
                             / static_cast<float>(plot_x1 - plot_x0);
            const float f = kFMin * std::pow(10.f, tn * kLogSpan);
            float db = TiltEq_CascadeMagnitudeDb(center_hz, tilt, f, kPlotSr, eq_q);
            if(db > 9.f)
                db = 9.f;
            if(db < -9.f)
                db = -9.f;
            const float yn = static_cast<float>(plot_y0)
                             + (9.f - db) / 18.f * static_cast<float>(plot_y1 - plot_y0);
            int y = static_cast<int>(yn + 0.5f);
            if(y < plot_y0)
                y = plot_y0;
            if(y > plot_y1)
                y = plot_y1;
            s_y[px] = static_cast<int16_t>(y);
        }
        s_last_curve_ms = now_ms;
        s_prev_c = t.eq_center_norm;
        s_prev_t = t.eq_tilt_db;
        s_prev_q = eq_q;
    }

    for(int px = plot_x0; px < plot_x1; ++px)
        d.DrawLine(px, s_y[px], px + 1, s_y[px + 1], true);

    auto hz_to_x = [&](float hz) -> int
    {
        const float lg = static_cast<float>(std::log10(static_cast<double>(hz / kFMin))) / kLogSpan;
        return plot_x0
               + static_cast<int>(lg * static_cast<float>(plot_x1 - plot_x0) + 0.5f);
    };

    // Tiny font: micro glyph set has no reliable digits (narrow "1" vanishes in 5x7->4x6 crop).
    const int lab_y = kDisplayH - Font5x7::H;
    auto draw_hz_label = [&](const char* s, float hz)
    {
        const int x = hz_to_x(hz);
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

    auto detail_param_count = [](uint8_t id) -> uint8_t
    {
        switch(id)
        {
            case 0: return 4; // SAT + mode toggle
            case 1: return 1; // EQ: graph only (no classic detail)
            case 2: return 4; // DELAY: LTM RTM FBK MIX
            case 3: return 5; // REVERB + DIR toggle
            default: return 3;
        }
    };

    // POD2 toggles layer (same behavior as other PERFORM pages).
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        app.perform_layer ^= 1u;
        const uint8_t layer = app.perform_layer & 1u;
        app.sd_current_slot.store(layer, std::memory_order_release);
        PublishEngineLayerParams(ctx);
        app.ui_dirty = true;
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
                const uint8_t layer = main_cursor & 1u; // 0=VOL A, 1=VOL B
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
                        if(restore < 0.0f) restore = 0.0f;
                        if(restore > 2.0f) restore = 2.0f;
                        app.perform_process_vol_muted[layer] = false;
                        t.engine_layer_master_level[layer] = restore;
                        app.perform_process_vol_pct[layer]
                            = static_cast<uint16_t>(restore * 100.0f + 0.5f);
                    }
                }
                ctx.params->PublishTargets();
                app.ui_dirty = true;
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
            const int count = static_cast<int>(detail_param_count(fx_id));
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
            ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }
        if(app.perform_process_detail_active)
        {
            PerformParamsTargets& t = ctx.params->EditTargets();
            const uint8_t pidx = app.perform_process_detail_param[cursor];
            bool changed = false;
            switch(fx_id)
            {
                case 0: // SAT
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
                        changed = true;
                    }
                    else if(pidx == 1)
                    {
                        if(t.sat_mode == 0)
                            t.sat_bump = Clamp01(t.sat_bump + delta);
                        else
                            t.sat_bit_smpl = Clamp01(t.sat_bit_smpl + delta);
                        changed = true;
                    }
                    else if(pidx == 2)
                    {
                        t.sat_mix = Clamp01(t.sat_mix + delta);
                        t.sat_on = (t.sat_mix > 0.001f);
                        changed = true;
                    }
                    else if(pidx == 3)
                    {
                        const int dir = (e.value > 0) ? 1 : -1;
                        int steps = (e.value > 0) ? e.value : -e.value;
                        while(steps-- > 0)
                            t.sat_mode = (dir > 0) ? ((t.sat_mode + 1u) & 1u)
                                                   : ((t.sat_mode == 0) ? 1u : 0u);
                        changed = true;
                    }
                    break;
                case 2: // DELAY
                    if(pidx == 0)
                    {
                        if(ctx.rshift)
                        {
                            t.delay_time_l = Clamp01(t.delay_time_l + delta);
                            t.delay_time_r = Clamp01(t.delay_time_r + delta);
                        }
                        else
                            t.delay_time_l = Clamp01(t.delay_time_l + delta);
                        changed = true;
                    }
                    else if(pidx == 1)
                    {
                        if(ctx.rshift)
                        {
                            t.delay_time_l = Clamp01(t.delay_time_l + delta);
                            t.delay_time_r = Clamp01(t.delay_time_r + delta);
                        }
                        else
                            t.delay_time_r = Clamp01(t.delay_time_r + delta);
                        changed = true;
                    }
                    else if(pidx == 2)
                    {
                        t.delay_feedback = Clamp01(t.delay_feedback + delta);
                        changed = true;
                    }
                    else if(pidx == 3)
                    {
                        t.delay_mix = Clamp01(t.delay_mix + delta);
                        t.delay_on = (t.delay_mix > 0.001f);
                        changed = true;
                    }
                    break;
                case 3: // REVERB
                default:
                    if(pidx == 0)
                    {
                        t.reverb_pre = Clamp01(t.reverb_pre + delta);
                        changed = true;
                    }
                    else if(pidx == 1)
                    {
                        t.reverb_damp = Clamp01(t.reverb_damp + delta);
                        changed = true;
                    }
                    else if(pidx == 2)
                    {
                        t.reverb_decay = Clamp01(t.reverb_decay + delta);
                        changed = true;
                    }
                    else if(pidx == 3)
                    {
                        t.reverb_mod = Clamp01(t.reverb_mod + delta);
                        changed = true;
                    }
                    else if(pidx == 4)
                    {
                        t.reverb_mix = Clamp01(t.reverb_mix + delta);
                        t.reverb_on = (t.reverb_mix > 0.001f);
                        changed = true;
                    }
                    break;
            }
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
            const uint8_t layer = main_cursor & 1u;
            PerformParamsTargets& t = ctx.params->EditTargets();
            if(app.perform_process_vol_muted[layer])
            {
                // Do not unmute on encoder turn; mute state toggles only on R-click.
                app.ui_dirty = true;
                return true;
            }

            // Match SETTINGS volume acceleration + range exactly.
            static uint32_t s_last_t_ms = 0;
            const uint32_t now_ms = e.t_ms;
            const uint32_t dt_ms  = (s_last_t_ms == 0) ? 999u : (now_ms - s_last_t_ms);
            s_last_t_ms = now_ms;

            float accel = 1.0f;
            if(dt_ms <= 25)       accel = 10.0f;
            else if(dt_ms <= 50)  accel = 6.0f;
            else if(dt_ms <= 90)  accel = 3.0f;
            else if(dt_ms <= 140) accel = 2.0f;

            const float base_step = 0.01f;
            float next = t.engine_layer_master_level[layer] + static_cast<float>(e.value) * base_step * accel;
            if(next < 0.0f) next = 0.0f;
            if(next > 2.0f) next = 2.0f;
            t.engine_layer_master_level[layer] = next;
            app.perform_process_vol_unmuted_level[layer] = next;
            app.perform_process_vol_pct[layer] = static_cast<uint16_t>(next * 100.0f + 0.5f);
            ctx.params->PublishTargets();
            app.ui_dirty = true;
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

    auto draw_process_knob = [&](int cx,
                                 int cy,
                                 int radius,
                                 char side_letter,
                                 const char* value_text,
                                 float angle_rad,
                                 bool focused)
    {
        DrawCirclePixels(d, cx, cy, radius, true);
        d.DrawPixel(cx, cy, true);
        const int hand_r = radius - 2;
        const int hx = cx + static_cast<int>(std::cos(angle_rad) * static_cast<float>(hand_r));
        const int hy = cy + static_cast<int>(std::sin(angle_rad) * static_cast<float>(hand_r));
        d.DrawLine(cx, cy, hx, hy, true);

        char side_text[2] = {side_letter, '\0'};
        const int label_w = TinyStringWidth(side_text);
        const int label_x = cx - radius - 9;
        const int label_y = cy - (Font5x7::H / 2);
        if(focused)
        {
            d.DrawRect(label_x - 3, label_y - 2, label_x + label_w + 2, label_y + Font5x7::H + 1, true, false);
            DrawTinyString(d, side_text, label_x, label_y, true);
        }
        else
        {
            DrawTinyString(d, side_text, label_x, label_y, true);
        }

        if(value_text && value_text[0] != '\0')
        {
            auto process_value_advance = [](char ch, char next_ch) -> int
            {
                if(ch == '.')
                    return 3;
                if(next_ch == 'd')
                    return 6;
                if(ch == 'd' && next_ch == 'b')
                    return 6;
                if(next_ch == '.')
                    return 5;
                return 5;
            };

            auto process_value_width = [&](const char* s) -> int
            {
                if(s == nullptr || s[0] == '\0')
                    return 0;
                int width = 0;
                for(int i = 0; s[i] != '\0'; ++i)
                    width += process_value_advance(s[i], s[i + 1]);
                return width;
            };

            auto draw_process_value = [&](const char* s, int x, int y)
            {
                int pen_x = x;
                for(int i = 0; s[i] != '\0'; ++i)
                {
                    char ch = s[i];
                    if(ch >= 'A' && ch <= 'Z')
                        ch = static_cast<char>(ch - 'A' + 'a');

                    uint8_t rows[Font5x7::H] = {};
                    Font5x7::GetGlyphRows(ch, rows);
                    for(int yy = 0; yy < Font5x7::H; ++yy)
                    {
                        const uint8_t row = rows[yy];
                        for(int xx = 0; xx < Font5x7::W; ++xx)
                        {
                            if((row >> (Font5x7::W - 1 - xx)) & 1)
                            {
                                const int px = pen_x + xx - ((ch == '.') ? 1 : 0);
                                const int py = y + yy;
                                if(px >= 0 && px < 128 && py >= 0 && py < 64)
                                    d.DrawPixel(px, py, true);
                            }
                        }
                    }
                    pen_x += process_value_advance(ch, s[i + 1]);
                }
            };

            auto draw_plus = [&](int x, int y)
            {
                d.DrawLine(x + 2, y + 1, x + 2, y + 5, true);
                d.DrawLine(x, y + 3, x + 4, y + 3, true);
            };

            if(value_text[0] == '+')
            {
                const char* rest = value_text + 1;
                const int rest_w = process_value_width(rest);
                const int value_w = 5 + 1 + rest_w;
                const int value_x = cx - (value_w / 2);
                const int value_y = cy - radius - 8;
                draw_plus(value_x, value_y);
                draw_process_value(rest, value_x + 6, value_y);
            }
            else
            {
                const int value_w = process_value_width(value_text);
                draw_process_value(value_text, cx - (value_w / 2), cy - radius - 8);
            }
        }
    };

    // Left pane: stacked layer volume knobs.
    constexpr int kLeftX = 0;
    constexpr int kLeftW = 60;
    const int left_y = box_y;
    const int left_h = box_h;
    if(left_h > 24)
    {
        const bool sel_a = (main_cursor == 0u);
        const bool sel_b = (main_cursor == 1u);

        const uint32_t a_pct = static_cast<uint32_t>(t.engine_layer_master_level[0] * 100.0f + 0.5f);
        const uint32_t b_pct = static_cast<uint32_t>(t.engine_layer_master_level[1] * 100.0f + 0.5f);
        app.perform_process_vol_pct[0] = static_cast<uint16_t>(a_pct);
        app.perform_process_vol_pct[1] = static_cast<uint16_t>(b_pct);

        char a_buf[12];
        char b_buf[12];
        FormatProcessLevelDb(t.engine_layer_master_level[0], a_buf, sizeof(a_buf));
        FormatProcessLevelDb(t.engine_layer_master_level[1], b_buf, sizeof(b_buf));
        const float a_norm = ProcessLevelToKnobNorm(t.engine_layer_master_level[0]);
        const float b_norm = ProcessLevelToKnobNorm(t.engine_layer_master_level[1]);
        const float a_angle = 2.0943951f + (a_norm * 5.2359878f);
        const float b_angle = 2.0943951f + (b_norm * 5.2359878f);

        constexpr int kVolKnobRadius = 9;
        const int knob_cx = kLeftX + (kLeftW / 2) - 1;
        const int a_cy = left_y + 13;
        const int b_cy = left_y + left_h - 13;
        draw_process_knob(knob_cx, a_cy, kVolKnobRadius, 'a', a_buf, a_angle, sel_a);
        draw_process_knob(knob_cx, b_cy, kVolKnobRadius, 'b', b_buf, b_angle, sel_b);
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

        if(ctx.rshift && selected_index >= 0 && selected_index < 4)
        {
            const int line_top = fader_y + 2;
            const int label_y = fader_y + fader_h - Font5x7::H - 1;
            const int line_bottom = label_y - 2;
            if(line_bottom > line_top)
            {
                const int fader_left = fader_x + 4;
                const int fader_right = fader_x + fader_w - 5;
                const int span_x = fader_right - fader_left;
                const int line_x = (span_x > 0)
                                       ? (fader_left + (span_x * selected_index) / 3)
                                       : fader_left;
                const int cy = line_top + ((line_bottom - line_top) / 2);
                const bool can_move_left = (selected_index > 0);
                const bool can_move_right = (selected_index < 3);

                auto draw_left_arrow = [&](int tip_x)
                {
                    d.DrawPixel(tip_x, cy, true);
                    d.DrawPixel(tip_x + 1, cy - 1, true);
                    d.DrawPixel(tip_x + 1, cy, true);
                    d.DrawPixel(tip_x + 1, cy + 1, true);
                    d.DrawPixel(tip_x + 2, cy - 2, true);
                    d.DrawPixel(tip_x + 2, cy - 1, true);
                    d.DrawPixel(tip_x + 2, cy, true);
                    d.DrawPixel(tip_x + 2, cy + 1, true);
                    d.DrawPixel(tip_x + 2, cy + 2, true);
                };

                auto draw_right_arrow = [&](int tip_x)
                {
                    d.DrawPixel(tip_x, cy, true);
                    d.DrawPixel(tip_x - 1, cy - 1, true);
                    d.DrawPixel(tip_x - 1, cy, true);
                    d.DrawPixel(tip_x - 1, cy + 1, true);
                    d.DrawPixel(tip_x - 2, cy - 2, true);
                    d.DrawPixel(tip_x - 2, cy - 1, true);
                    d.DrawPixel(tip_x - 2, cy, true);
                    d.DrawPixel(tip_x - 2, cy + 1, true);
                    d.DrawPixel(tip_x - 2, cy + 2, true);
                };

                if(can_move_left)
                    draw_left_arrow(line_x - 7);
                if(can_move_right)
                    draw_right_arrow(line_x + 7);
            }
        }
    }
}
