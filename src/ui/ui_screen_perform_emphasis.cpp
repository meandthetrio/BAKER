#include "ui_screens_internal.h"

#include <cmath>
#include <cstdio>

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "oled_pager.h"
#include "params.h"
#include "ui_input.h"

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static void FormatDbTenths(int v_tenths, char* out, size_t out_n)
{
    const int sign = (v_tenths < 0) ? -1 : 1;
    const int abs_tenths = (v_tenths < 0) ? -v_tenths : v_tenths;
    const int whole = abs_tenths / 10;
    const int frac = abs_tenths % 10;
    if(sign < 0)
        std::snprintf(out, out_n, "-%d.%ddB", whole, frac);
    else
        std::snprintf(out, out_n, "%d.%ddB", whole, frac);
}

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

static float UiDeltaNormAccelerated(int enc_delta, uint32_t t_ms, uint32_t& last_t_ms, float base_step)
{
    const uint32_t dt_ms = (last_t_ms == 0u) ? 999u : (t_ms - last_t_ms);
    last_t_ms = t_ms;
    return static_cast<float>(enc_delta) * base_step * UiAccelFromDtMs(dt_ms);
}

static float AdsrFltFaderFromCutoffHz(float hz)
{
    if(hz < 20.0f) hz = 20.0f;
    if(hz > 20000.0f) hz = 20000.0f;
    const float shaped = std::log(hz / 20.0f) / std::log(20000.0f / 20.0f);
    const float v = shaped * shaped;
    return Clamp01(v);
}

static float AdsrFltCutoffHzFromFader(float value)
{
    value = Clamp01(value);
    const float shaped = std::sqrt(value);
    return 20.0f * std::pow(20000.0f / 20.0f, shaped);
}

static uint8_t ClampDriveModeLocal(int value)
{
    if(value <= 0)
        return 0u;
    return 1u;
}

static const char* DriveModeLabel(uint8_t mode)
{
    return (ClampDriveModeLocal(static_cast<int>(mode)) == 0u) ? "odd" : "even";
}

bool PerformEmphasis_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.params)
        return false;
    if(ctx.shift)
        return false;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;
    const uint8_t layer = engine.perform_layer & 1u;

    static uint32_t s_last_ext_t_ms = 0u;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        int row = static_cast<int>(engine.perform_emphasis_row) + e.value;
        while(row < 0) row += 3;
        while(row >= 3) row -= 3;
        engine.perform_emphasis_row = static_cast<uint8_t>(row);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const float delta_norm = UiDeltaNormAccelerated(e.value, e.t_ms, s_last_ext_t_ms, 0.02f);
        if(engine.perform_emphasis_row == 0)
        {
            if(ctx.rshift)
            {
                int mode = static_cast<int>(engine.engine_drive_mode[layer]) + e.value;
                while(mode < 0)
                    mode += 2;
                while(mode >= 2)
                    mode -= 2;
                const uint8_t next_mode = ClampDriveModeLocal(mode);
                if(next_mode != engine.engine_drive_mode[layer])
                {
                    engine.engine_drive_mode[layer] = next_mode;
                    PublishEngineLayerParams(ctx);
                    ui.ui_dirty = true;
                }
                return true;
            }

            // DRIVE row: 0.0 dB at 7 o'clock through +6.0 dB at 5 o'clock in 0.1 dB steps.
            int v = static_cast<int>(engine.engine_gain_db[layer]) + e.value;
            v = ClampInt(v, 0, 60);
            const int16_t vv = static_cast<int16_t>(v);
            if(vv != engine.engine_gain_db[layer])
            {
                engine.engine_gain_db[layer] = vv;
                PublishEngineLayerParams(ctx);
                ui.ui_dirty = true;
            }
            return true;
        }

        PerformParamsTargets& t = ctx.params->EditTargets();
        if(engine.perform_emphasis_row == 1)
        {
            // Keep fast fader motion; map fader space to cutoff using ADSR-style curve.
            float fader = AdsrFltFaderFromCutoffHz(t.engine_filter_cutoff_hz[layer]);
            fader = Clamp01(fader + delta_norm);
            t.engine_filter_cutoff_hz[layer] = AdsrFltCutoffHzFromFader(fader);
            ctx.params->PublishTargets();
            ui.ui_dirty = true;
            return true;
        }

        // RESONANCE
        t.engine_filter_resonance[layer] = Clamp01(t.engine_filter_resonance[layer] + delta_norm);
        ctx.params->PublishTargets();
        ui.ui_dirty = true;
        return true;
    }

    // POD2 toggles layer (same behavior as ENGINE).
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        engine.perform_layer ^= 1u;
        const uint8_t layer = engine.perform_layer & 1u;
        shared.sample.sd_current_slot.store(layer, std::memory_order_release);
        engine.engine_header_invert_until_ms = e.t_ms + 250u;
        PublishEngineLayerParams(ctx);
        ui.ui_dirty = true;
        return true;
    }

    return false;
}

void PerformEmphasis_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;
    ctx.ui->ui_dirty = true;
}

void PerformEmphasis_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display || !ctx.params)
        return;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;
    EngineRefreshLoadedMetadata(ui, engine, shared);

    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t layer = engine.perform_layer & 1u;
    const PerformParamsTargets& t = ctx.params->TargetsForUI();
    const float cutoff_hz = t.engine_filter_cutoff_hz[layer];
    const float resonance = Clamp01(t.engine_filter_resonance[layer]);
    char header_label[16] = {};
    std::snprintf(header_label, sizeof(header_label), "emph %c", layer == 0 ? 'a' : 'b');
    const int header_w = MicroStringWidth(header_label);
    const int box_w = header_w + 4;
    const int box_h = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash
        = static_cast<int32_t>(engine.engine_header_invert_until_ms - ctx.now_ms) > 0;
    if(header_invert_flash)
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, false, true);
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, false);
        DrawMicroString(d, header_label, box_x + 2, 2, true);
    }
    else
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, true);
        DrawMicroString(d, header_label, box_x + 2, 2, false);
    }

    auto clamp01f = [](float v) -> float
    {
        if(v < 0.0f) return 0.0f;
        if(v > 1.0f) return 1.0f;
        return v;
    };

    auto draw_knob = [&](int cx,
                         int cy,
                         int radius,
                         const char* label,
                         const char* value_text,
                         float angle_rad,
                         int focus_style)
    {
        DrawCirclePixels(d, cx, cy, radius, true);
        d.DrawPixel(cx, cy, true);
        const int hand_r = radius - 2;
        const int hx = cx + static_cast<int>(std::cos(angle_rad) * static_cast<float>(hand_r));
        const int hy = cy + static_cast<int>(std::sin(angle_rad) * static_cast<float>(hand_r));
        d.DrawLine(cx, cy, hx, hy, true);

        const int label_w = TinyStringWidth(label);
        const int label_x = cx - (label_w / 2);
        const int label_y = cy + radius + 5;
        if(focus_style == 1)
        {
            d.DrawRect(label_x - 2, label_y - 1, label_x + label_w + 1, label_y + Font5x7::H, true, false);
            DrawTinyString(d, label, label_x, label_y, true);
        }
        else if(focus_style == 2)
        {
            DrawDottedRect(d, label_x - 2, label_y - 1, label_x + label_w + 1, label_y + Font5x7::H, true);
            DrawTinyString(d, label, label_x, label_y, true);
        }
        else
        {
            DrawTinyString(d, label, label_x, label_y, true);
        }
        if(value_text && value_text[0] != '\0')
        {
            const int value_w = TinyStringWidth(value_text);
            DrawTinyString(d, value_text, cx - (value_w / 2), cy - radius - 8, true);
        }
    };

    char gain_buf[12];
    FormatDbTenths(engine.engine_gain_db[layer], gain_buf, sizeof(gain_buf));
    const bool drive_mode_focus = (engine.perform_emphasis_row == 0u) && ctx.rshift;
    const char* drive_label = drive_mode_focus ? DriveModeLabel(engine.engine_drive_mode[layer]) : "drive";
    const char* drive_value = drive_mode_focus ? "" : gain_buf;

    float cutoff = cutoff_hz;
    if(cutoff < 20.0f) cutoff = 20.0f;
    if(cutoff > 20000.0f) cutoff = 20000.0f;
    char cutoff_buf[16];
    const uint32_t lpf_hz = static_cast<uint32_t>(cutoff + 0.5f);
    if(lpf_hz >= 1000u)
    {
        if((lpf_hz % 1000u) == 0u)
            std::snprintf(cutoff_buf, sizeof(cutoff_buf), "%luk", (unsigned long)(lpf_hz / 1000u));
        else
            std::snprintf(cutoff_buf,
                          sizeof(cutoff_buf),
                          "%lu.%01luk",
                          (unsigned long)(lpf_hz / 1000u),
                          (unsigned long)((lpf_hz % 1000u) / 100u));
    }
    else
        std::snprintf(cutoff_buf, sizeof(cutoff_buf), "%lu", (unsigned long)lpf_hz);

    const int gain_tenths = static_cast<int>(engine.engine_gain_db[layer]);
    const float gain_norm = clamp01f(static_cast<float>(gain_tenths) / 60.0f);
    const float gain_angle = 2.0943951f + (gain_norm * 5.2359878f);

    const float cutoff_norm = AdsrFltFaderFromCutoffHz(cutoff);
    const float cutoff_angle = 2.0943951f + (cutoff_norm * 5.2359878f);
    const float reso_angle = 2.0943951f + (resonance * 5.2359878f);

    constexpr int kKnobRadius = 12;
    constexpr int kKnobCy = 28;
    constexpr int kKnobCx[3] = {22, 64, 106};
    draw_knob(kKnobCx[0],
              kKnobCy,
              kKnobRadius,
              drive_label,
              drive_value,
              gain_angle,
              engine.perform_emphasis_row == 0 ? (drive_mode_focus ? 2 : 1) : 0);
    draw_knob(kKnobCx[1],
              kKnobCy,
              kKnobRadius,
              "cutoff",
              cutoff_buf,
              cutoff_angle,
              engine.perform_emphasis_row == 1 ? 2 : 0);
    draw_knob(kKnobCx[2],
              kKnobCy,
              kKnobRadius,
              "reso",
              "",
              reso_angle,
              engine.perform_emphasis_row == 2 ? 2 : 0);
}
