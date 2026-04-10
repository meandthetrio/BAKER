#include "ui_screens_internal.h"

#include <cmath>
#include <cstdio>

#include "app_state.h"
#include "oled_pager.h"
#include "sample_edit.h"
#include "ui_input.h"

static constexpr int32_t kAdsrRowCount = 3;
static constexpr int32_t kAdsrRowOneShot = 0;
static constexpr int32_t kAdsrRowLoop = 1;
static constexpr int32_t kAdsrRowAdsr = 2;
static constexpr int32_t kAdsrStageCount = 4;
static constexpr float kPerformLoopCrossfadeMin = 0.0f;
static constexpr float kPerformLoopCrossfadeMax = 0.5f;
static constexpr float kPerformLoopCrossfadeStep = 1.0f / 128.0f;
static constexpr float kPerformLoopCrossfadeShapeMin = 0.0f;
static constexpr float kPerformLoopCrossfadeShapeMax = 1.0f;
static constexpr float kPerformLoopCrossfadeShapeStep = 1.0f / 64.0f;
static constexpr uint16_t kPerformAdsrAttackReleaseMinMs = 1u;
static constexpr uint16_t kPerformAdsrAttackReleaseMaxMs = 1000u;
static constexpr uint16_t kPerformAdsrDecayMaxMs = 100u;
static constexpr uint16_t kPerformAdsrSustainMax = 100u;

static float Clamp01(float x)
{
    if(x < 0.0f) return 0.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static float ComputePerformLoopCrossfadeWeight(float mix, float shape, bool fade_in)
{
    mix = Clamp01(mix);
    shape = Clamp01(shape);
    const float linear = fade_in ? mix : (1.0f - mix);
    const float equal_power = fade_in ? std::sqrt(mix) : std::sqrt(1.0f - mix);
    return linear + (equal_power - linear) * shape;
}

static uint16_t PerformAdsrStageValue(const AppState& app, uint8_t layer, uint8_t stage)
{
    const uint8_t safe_layer = layer & 1u;
    switch(stage % static_cast<uint8_t>(kAdsrStageCount))
    {
        case 0: return app.engine.perform_adsr_loop_attack[safe_layer];
        case 1: return app.engine.perform_adsr_loop_decay[safe_layer];
        case 2: return app.engine.perform_adsr_loop_sustain[safe_layer];
        default: return app.engine.perform_adsr_loop_release[safe_layer];
    }
}

static void SetPerformAdsrStageValue(AppState& app, uint8_t layer, uint8_t stage, uint16_t value)
{
    const uint8_t safe_layer = layer & 1u;
    switch(stage % static_cast<uint8_t>(kAdsrStageCount))
    {
        case 0:
            app.engine.perform_adsr_loop_attack[safe_layer] = value;
            return;
        case 1:
            app.engine.perform_adsr_loop_decay[safe_layer] = static_cast<uint8_t>(value);
            return;
        case 2:
            app.engine.perform_adsr_loop_sustain[safe_layer] = static_cast<uint8_t>(value);
            return;
        default:
            app.engine.perform_adsr_loop_release[safe_layer] = value;
            return;
    }
}

static int PerformAdsrStageMin(uint8_t stage)
{
    return (stage % static_cast<uint8_t>(kAdsrStageCount)) == 2u
               ? 0
               : static_cast<int>(kPerformAdsrAttackReleaseMinMs);
}

static int PerformAdsrStageMax(uint8_t stage)
{
    switch(stage % static_cast<uint8_t>(kAdsrStageCount))
    {
        case 0:
        case 3: return static_cast<int>(kPerformAdsrAttackReleaseMaxMs);
        case 1: return static_cast<int>(kPerformAdsrDecayMaxMs);
        case 2:
        default: return static_cast<int>(kPerformAdsrSustainMax);
    }
}

static uint8_t& PerformAdsrRow(AppState& app, uint8_t layer)
{
    return app.engine.perform_adsr_row[layer & 1u];
}

static uint8_t& PerformAdsrEnvX(AppState& app, uint8_t layer, uint8_t stage)
{
    const uint8_t safe_layer = layer & 1u;
    switch(stage % static_cast<uint8_t>(kAdsrStageCount))
    {
        case 0: return app.engine.perform_adsr_env_a_x[safe_layer];
        case 1: return app.engine.perform_adsr_env_d_x[safe_layer];
        default: return app.engine.perform_adsr_env_r_x[safe_layer];
    }
}

static uint8_t& PerformAdsrEnvSLevel(AppState& app, uint8_t layer)
{
    return app.engine.perform_adsr_env_s_level[layer & 1u];
}

static bool PerformAdsrStageEnabled(uint8_t adsr_row, uint8_t stage)
{
    const uint8_t safe_row = adsr_row % static_cast<uint8_t>(kAdsrRowCount);
    const uint8_t safe_stage = stage % static_cast<uint8_t>(kAdsrStageCount);
    if(safe_row == static_cast<uint8_t>(kAdsrRowOneShot)
       && (safe_stage == 1u || safe_stage == 2u))
        return false;
    return true;
}

static bool PerformAdsrWaveFocusable(uint8_t adsr_row)
{
    return (adsr_row % static_cast<uint8_t>(kAdsrRowCount)) == static_cast<uint8_t>(kAdsrRowLoop);
}

static int PerformAdsrFocusIndex(const AppState& app, uint8_t layer)
{
    if(app.engine.perform_adsr_type_focus)
        return 0;

    const uint8_t adsr_row = app.engine.perform_adsr_row[layer & 1u];
    if(PerformAdsrWaveFocusable(adsr_row) && app.engine.perform_adsr_wave_focus)
        return 1;

    return static_cast<int>(app.engine.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount))
           + (PerformAdsrWaveFocusable(adsr_row) ? 2 : 1);
}

static void PerformAdsrSetFocusIndex(AppState& app, uint8_t layer, int idx)
{
    const uint8_t adsr_row = app.engine.perform_adsr_row[layer & 1u];
    if(idx <= 0)
    {
        app.engine.perform_adsr_type_focus = true;
        app.engine.perform_adsr_wave_focus = false;
        return;
    }

    if(PerformAdsrWaveFocusable(adsr_row) && idx == 1)
    {
        app.engine.perform_adsr_type_focus = false;
        app.engine.perform_adsr_wave_focus = true;
        return;
    }

    app.engine.perform_adsr_type_focus = false;
    app.engine.perform_adsr_wave_focus = false;
    const int stage_base = PerformAdsrWaveFocusable(adsr_row) ? 2 : 1;
    app.engine.perform_adsr_stage_focus
        = static_cast<uint8_t>(ClampInt(idx - stage_base, 0, kAdsrStageCount - 1));
}

static void PerformAdsrEnsureValidFocus(AppState& app, uint8_t layer)
{
    const uint8_t adsr_row = PerformAdsrRow(app, layer);
    if(app.engine.perform_adsr_type_focus)
    {
        app.engine.perform_adsr_wave_focus = false;
        return;
    }

    if(!PerformAdsrWaveFocusable(adsr_row))
        app.engine.perform_adsr_wave_focus = false;
    if(app.engine.perform_adsr_wave_focus)
        return;

    const uint8_t stage = app.engine.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
    if(PerformAdsrStageEnabled(adsr_row, stage))
        return;

    app.engine.perform_adsr_stage_focus = (stage <= 1u) ? 0u : 3u;
}

static void DrawPerformLoopCrossfadeCurve(OledPager& d,
                                          int x0,
                                          int y0,
                                          int x1,
                                          int y1,
                                          float shape,
                                          bool fade_in)
{
    if(x1 <= x0 || y1 <= y0)
        return;

    const int w = x1 - x0 + 1;
    int prev_x = x0;
    int prev_y = y1;
    for(int i = 0; i < w; ++i)
    {
        const float mix = (w <= 1) ? 0.0f : (static_cast<float>(i) / static_cast<float>(w - 1));
        const float weight = ComputePerformLoopCrossfadeWeight(mix, shape, fade_in);
        const int xx = x0 + i;
        const int yy = y1 - static_cast<int>(weight * static_cast<float>(y1 - y0));
        if(i == 0)
            d.DrawPixel(xx, yy, true);
        else
            d.DrawLine(prev_x, prev_y, xx, yy, true);
        prev_x = xx;
        prev_y = yy;
    }
}

bool PerformAdsr_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    if(ctx.shift)
        return false;

    AppState& app = *ctx.app;

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        app.engine.perform_layer ^= 1u;
        const uint8_t layer = app.engine.perform_layer & 1u;
        app.shared.sd_current_slot.store(layer, std::memory_order_release);
        app.engine.engine_header_invert_until_ms = e.t_ms + 250u;
        PerformAdsrEnsureValidFocus(app, layer);
        PublishEngineLayerParams(ctx);
        app.ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        const uint8_t layer = app.engine.perform_layer & 1u;
        const uint8_t adsr_row = PerformAdsrRow(app, layer);
        int idx = PerformAdsrFocusIndex(app, layer);
        const bool wave_focusable = PerformAdsrWaveFocusable(adsr_row);
        const int focus_count = kAdsrStageCount + (wave_focusable ? 2 : 1);
        const int stage_base = wave_focusable ? 2 : 1;
        int steps = (e.value < 0) ? -e.value : e.value;
        const int dir = (e.value < 0) ? -1 : 1;
        while(steps-- > 0)
        {
            do
            {
                idx += dir;
                while(idx < 0)
                    idx += focus_count;
                while(idx >= focus_count)
                    idx -= focus_count;
            } while(idx >= stage_base
                    && !PerformAdsrStageEnabled(adsr_row,
                                                static_cast<uint8_t>(idx - stage_base)));
        }

        PerformAdsrSetFocusIndex(app, layer, idx);
        app.ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const uint8_t layer = app.engine.perform_layer & 1u;
        uint8_t& adsr_row = PerformAdsrRow(app, layer);
        if(app.engine.perform_adsr_type_focus)
        {
            int row = static_cast<int>(adsr_row % static_cast<uint8_t>(kAdsrRowCount));
            row += e.value;
            while(row < 0)
                row += kAdsrRowCount;
            while(row >= kAdsrRowCount)
                row -= kAdsrRowCount;
            adsr_row = static_cast<uint8_t>(row);

            uint8_t next_mode = app.engine.engine_play_mode[layer] & 1u;
            bool changed = false;
            if(row == static_cast<int>(kAdsrRowOneShot))
            {
                if(next_mode != 0u)
                {
                    next_mode = 0u;
                    changed = true;
                }
            }
            else if(row == static_cast<int>(kAdsrRowLoop))
            {
                if(next_mode != 1u)
                {
                    next_mode = 1u;
                    changed = true;
                }
            }

            if(changed)
            {
                app.engine.engine_play_mode[layer] = next_mode;
                PublishEngineLayerParams(ctx);
            }
            PerformAdsrEnsureValidFocus(app, layer);
            app.ui.ui_dirty = true;
            return true;
        }

        if(app.engine.perform_adsr_wave_focus
           && (adsr_row % static_cast<uint8_t>(kAdsrRowCount)) == static_cast<uint8_t>(kAdsrRowLoop))
        {
            float& target = ctx.rshift ? app.engine.perform_adsr_loop_crossfade_shape[layer]
                                       : app.engine.perform_adsr_loop_crossfade[layer];
            const float step = ctx.rshift ? kPerformLoopCrossfadeShapeStep
                                          : kPerformLoopCrossfadeStep;
            const float min_value = ctx.rshift ? kPerformLoopCrossfadeShapeMin
                                               : kPerformLoopCrossfadeMin;
            const float max_value = ctx.rshift ? kPerformLoopCrossfadeShapeMax
                                               : kPerformLoopCrossfadeMax;
            float next = target + (static_cast<float>(e.value) * step);
            if(next < min_value)
                next = min_value;
            if(next > max_value)
                next = max_value;
            if(next == target)
                return false;
            target = next;
            PublishEngineLayerParams(ctx);
            app.ui.ui_dirty = true;
            return true;
        }

        if((adsr_row % static_cast<uint8_t>(kAdsrRowCount)) != static_cast<uint8_t>(kAdsrRowLoop))
        {
            if((adsr_row % static_cast<uint8_t>(kAdsrRowCount)) != static_cast<uint8_t>(kAdsrRowAdsr))
                return false;

            const uint8_t stage = app.engine.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
            if(stage == 2u)
            {
                uint8_t& level = PerformAdsrEnvSLevel(app, layer);
                const int next_level = ClampInt(static_cast<int>(level) + e.value, 0, 100);
                if(next_level == static_cast<int>(level))
                    return false;
                level = static_cast<uint8_t>(next_level);
                app.ui.ui_dirty = true;
                return true;
            }

            static constexpr int kAdsrEnvMinGap = 6;
            uint8_t& value = PerformAdsrEnvX(app, layer, stage);
            const int a_x = static_cast<int>(app.engine.perform_adsr_env_a_x[layer]);
            const int d_x = static_cast<int>(app.engine.perform_adsr_env_d_x[layer]);
            const int r_x = static_cast<int>(app.engine.perform_adsr_env_r_x[layer]);
            int min_value = 0;
            int max_value = 100;
            if(stage == 0u)
            {
                max_value = d_x - kAdsrEnvMinGap;
            }
            else if(stage == 1u)
            {
                min_value = a_x + kAdsrEnvMinGap;
                max_value = r_x - kAdsrEnvMinGap;
            }
            else
            {
                min_value = d_x + kAdsrEnvMinGap;
            }

            const int next_value = ClampInt(static_cast<int>(value) + e.value, min_value, max_value);
            if(next_value == static_cast<int>(value))
                return false;
            value = static_cast<uint8_t>(next_value);
            app.ui.ui_dirty = true;
            return true;
        }

        const uint8_t stage = app.engine.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
        const uint16_t value = PerformAdsrStageValue(app, layer, stage);
        const int min_value = PerformAdsrStageMin(stage);
        const int max_value = PerformAdsrStageMax(stage);
        const int next_value = ClampInt(static_cast<int>(value) + e.value, min_value, max_value);
        if(next_value == static_cast<int>(value))
            return false;

        SetPerformAdsrStageValue(app, layer, stage, static_cast<uint16_t>(next_value));
        PublishEngineLayerParams(ctx);
        app.ui.ui_dirty = true;
        return true;
    }

    return false;
}

void PerformAdsr_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    AppState& app = *ctx.app;
    app.engine.perform_adsr_stage_focus = 0;
    app.engine.perform_adsr_type_focus = false;
    app.engine.perform_adsr_wave_focus = false;
    const uint8_t layer = app.engine.perform_layer & 1u;
    uint8_t& adsr_row = PerformAdsrRow(app, layer);
    if(adsr_row >= static_cast<uint8_t>(kAdsrRowCount))
        adsr_row = (app.engine.engine_play_mode[layer] & 1u) ? kAdsrRowLoop : kAdsrRowOneShot;
    PerformAdsrEnsureValidFocus(app, layer);
    app.ui.ui_dirty = true;
}

void PerformAdsr_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);

    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t layer = app.engine.perform_layer & 1u;
    const uint8_t adsr_row = PerformAdsrRow(app, layer);
    const Sample& sample = app.shared.sd_slots[layer];
    const bool sample_loaded = (sample.pcm != nullptr && sample.length > 0);
    const SampleEdit* edit = sample_loaded ? &app.shared.sd_edit_slots[layer] : nullptr;

    char header_label[16] = {};
    std::snprintf(header_label, sizeof(header_label), "adsr %c", layer == 0 ? 'a' : 'b');
    const int header_w = MicroStringWidth(header_label);
    const int box_w = header_w + 4;
    const int box_h = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash
        = static_cast<int32_t>(app.engine.engine_header_invert_until_ms - ctx.now_ms) > 0;
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

    constexpr int kWaveX = 0;
    constexpr int kWaveY = 10;
    constexpr int kWaveW = 128;

    const char* kPlaybackTypeLabel = "playback type";
    const bool type_focused = app.engine.perform_adsr_type_focus;
    const bool wave_focused = (!type_focused && app.engine.perform_adsr_wave_focus
                               && PerformAdsrWaveFocusable(adsr_row));
    const int label_area_x0 = 0;
    const int label_area_x1 = box_x - 1;
    if(label_area_x1 >= label_area_x0)
    {
        const int label_area_w = label_area_x1 - label_area_x0 + 1;
        const int max_chars = (label_area_w + 1) / kMicroAdvance;
        if(max_chars > 0)
        {
            char clipped[24] = {};
            int i = 0;
            for(; kPlaybackTypeLabel[i] != '\0' && i < max_chars
                  && i + 1 < static_cast<int>(sizeof(clipped));
                ++i)
                clipped[i] = kPlaybackTypeLabel[i];
            clipped[i] = '\0';
            const int draw_w = MicroStringWidth(clipped);
            int draw_x = label_area_x0 + ((label_area_w - draw_w) / 2);
            if(draw_x < label_area_x0)
                draw_x = label_area_x0;
            int draw_y = (kWaveY - kMicroH) / 2;
            if(draw_y < 0)
                draw_y = 0;

            constexpr int kTypeFocusPadX = 5;
            constexpr int kTypeFocusPadY = 2;
            int rx0 = draw_x - kTypeFocusPadX;
            int ry0 = draw_y - kTypeFocusPadY;
            int rx1 = draw_x + draw_w + (kTypeFocusPadX - 1);
            int ry1 = draw_y + kMicroH + (kTypeFocusPadY - 1);
            if(rx0 < label_area_x0)
                rx0 = label_area_x0;
            if(rx1 > label_area_x1)
                rx1 = label_area_x1;
            if(ry0 < 0)
                ry0 = 0;
            if(ry1 > (kWaveY - 1))
                ry1 = kWaveY - 1;

            if(type_focused)
            {
                DrawDottedRect(d, rx0, ry0, rx1, ry1, true);
                const char* kTypeValues[kAdsrRowCount] = {"1shot", "loop", "adsr"};
                const char* selected = kTypeValues[adsr_row % static_cast<uint8_t>(kAdsrRowCount)];
                const int value_w = MiniString3x5Width(selected);
                int value_x = rx0 + (((rx1 - rx0 + 1) - value_w) / 2);
                int value_y = ry0 + (((ry1 - ry0 + 1) - kMini3x5H) / 2);
                if(value_x < label_area_x0)
                    value_x = label_area_x0;
                if(value_y < 0)
                    value_y = 0;
                if(value_x + value_w - 1 > label_area_x1)
                    value_x = label_area_x1 - value_w + 1;
                DrawMiniString3x5(d, selected, value_x, value_y, true);
            }
            else
            {
                DrawMicroString(d, clipped, draw_x, draw_y, true);
            }
        }
    }

    int kWaveBottomY = static_cast<int>(d.Height()) - 8;
    if(kWaveBottomY < kWaveY)
        kWaveBottomY = kWaveY;
    const int kWaveH = kWaveBottomY - kWaveY + 1;
    const bool adsr_mode
        = (adsr_row % static_cast<uint8_t>(kAdsrRowCount)) == static_cast<uint8_t>(kAdsrRowAdsr);

    DrawWaveformPreview(
        d, sample, edit, kWaveX, kWaveY, kWaveW, kWaveH, true, adsr_mode, wave_focused);

    static const char* kBottomLetters[4] = {"a", "d", "s", "r"};
    const int bottom_y = kWaveBottomY + 2;
    const int seg_w = static_cast<int>(d.Width()) / 4;
    const uint8_t stage_focus = app.engine.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
    const bool loop_stage_editing
        = (adsr_row % static_cast<uint8_t>(kAdsrRowCount)) == static_cast<uint8_t>(kAdsrRowLoop);
    const int preview_x0 = kWaveX + 1;
    const int preview_x1 = kWaveX + kWaveW - 2;
    const int preview_y0 = kWaveY + 1;
    const int preview_y1 = kWaveBottomY - 1;
    const int preview_w = preview_x1 - preview_x0;
    const int preview_h = preview_y1 - preview_y0;

    if(wave_focused && sample_loaded && preview_x1 >= preview_x0 && preview_y1 >= preview_y0)
    {
        const int preview_pixels = preview_x1 - preview_x0 + 1;
        int crossfade_px = static_cast<int>(
            (static_cast<float>(preview_pixels - 1) * app.engine.perform_adsr_loop_crossfade[layer]) + 0.5f);
        if(crossfade_px < 0)
            crossfade_px = 0;
        const int max_crossfade_px = (preview_pixels - 1) / 2;
        if(crossfade_px > max_crossfade_px)
            crossfade_px = max_crossfade_px;

        const int left_bar_x = preview_x0 + crossfade_px;
        const int right_bar_x = preview_x1 - crossfade_px;
        if(ctx.rshift)
        {
            for(int yy = preview_y0; yy <= preview_y1; ++yy)
            {
                for(int xx = preview_x0; xx <= left_bar_x; ++xx)
                {
                    if(((xx + yy) & 1) != 0)
                        d.DrawPixel(xx, yy, false);
                }
                for(int xx = right_bar_x; xx <= preview_x1; ++xx)
                {
                    if(((xx + yy) & 1) != 0)
                        d.DrawPixel(xx, yy, false);
                }
            }
            DrawPerformLoopCrossfadeCurve(d,
                                          preview_x0,
                                          preview_y0,
                                          left_bar_x,
                                          preview_y1,
                                          app.engine.perform_adsr_loop_crossfade_shape[layer],
                                          false);
            DrawPerformLoopCrossfadeCurve(d,
                                          right_bar_x,
                                          preview_y0,
                                          preview_x1,
                                          preview_y1,
                                          app.engine.perform_adsr_loop_crossfade_shape[layer],
                                          true);
            d.DrawRect(preview_x0, preview_y0, left_bar_x, preview_y1, true, false);
            d.DrawRect(right_bar_x, preview_y0, preview_x1, preview_y1, true, false);
        }
        else
        {
            for(int yy = preview_y0; yy <= preview_y1; ++yy)
            {
                for(int xx = preview_x0; xx < left_bar_x; ++xx)
                {
                    if(((xx + yy) & 1) == 0)
                        d.DrawPixel(xx, yy, true);
                }
                for(int xx = right_bar_x + 1; xx <= preview_x1; ++xx)
                {
                    if(((xx + yy) & 1) == 0)
                        d.DrawPixel(xx, yy, true);
                }
            }
        }
        d.DrawLine(left_bar_x, preview_y0, left_bar_x, preview_y1, true);
        d.DrawLine(right_bar_x, preview_y0, right_bar_x, preview_y1, true);
    }

    if(adsr_mode)
    {
        const int a_x = preview_x0 + (preview_w * static_cast<int>(app.engine.perform_adsr_env_a_x[layer])) / 100;
        const int d_x = preview_x0 + (preview_w * static_cast<int>(app.engine.perform_adsr_env_d_x[layer])) / 100;
        const int r_x = preview_x0 + (preview_w * static_cast<int>(app.engine.perform_adsr_env_r_x[layer])) / 100;
        const int sustain_y
            = preview_y1 - (preview_h * static_cast<int>(app.engine.perform_adsr_env_s_level[layer])) / 100;

        d.DrawLine(a_x, kWaveY + 1, a_x, kWaveBottomY - 1, true);
        d.DrawLine(d_x, kWaveY + 1, d_x, kWaveBottomY - 1, true);
        d.DrawLine(r_x, kWaveY + 1, r_x, kWaveBottomY - 1, true);

        d.DrawLine(preview_x0, preview_y1, a_x, preview_y0, true);
        d.DrawLine(a_x, preview_y0, d_x, sustain_y, true);
        d.DrawLine(d_x, sustain_y, r_x, sustain_y, true);
        d.DrawLine(r_x, sustain_y, preview_x1, preview_y1, true);
    }

    for(int i = 0; i < 4; ++i)
    {
        const int seg_start = i * seg_w;
        const int seg_end = (i == 3) ? (static_cast<int>(d.Width()) - 1) : ((i + 1) * seg_w - 1);
        const int seg_center = seg_start + ((seg_end - seg_start + 1) / 2);
        const bool stage_enabled = PerformAdsrStageEnabled(adsr_row, static_cast<uint8_t>(i));

        if(adsr_mode)
        {
            const int w = MiniString3x5Width(kBottomLetters[i]);
            int box_center = seg_center;
            if(i == 0)
                box_center = preview_x0 + (preview_w * static_cast<int>(app.engine.perform_adsr_env_a_x[layer])) / 100;
            else if(i == 1)
                box_center = preview_x0 + (preview_w * static_cast<int>(app.engine.perform_adsr_env_d_x[layer])) / 100;
            else if(i == 2)
            {
                const int d_x
                    = preview_x0 + (preview_w * static_cast<int>(app.engine.perform_adsr_env_d_x[layer])) / 100;
                const int r_x
                    = preview_x0 + (preview_w * static_cast<int>(app.engine.perform_adsr_env_r_x[layer])) / 100;
                box_center = d_x + ((r_x - d_x) / 2);
            }
            else
                box_center = preview_x0 + (preview_w * static_cast<int>(app.engine.perform_adsr_env_r_x[layer])) / 100;
            const int box_w = w + 6;
            int box_x0 = box_center - (box_w / 2);
            int box_x1 = box_x0 + box_w - 1;
            if(box_x0 < seg_start)
            {
                box_x0 = seg_start;
                box_x1 = box_x0 + box_w - 1;
            }
            if(box_x1 > seg_end)
            {
                box_x1 = seg_end;
                box_x0 = box_x1 - box_w + 1;
            }

            const int box_y0 = bottom_y - 2;
            const int box_y1 = bottom_y + kMini3x5H + 1;
            const int value_x = box_x0 + ((box_w - w) / 2);
            const int value_y = bottom_y;
            if(!type_focused && stage_focus == static_cast<uint8_t>(i))
            {
                if(i == 2)
                {
                    d.DrawRect(box_x0, box_y0, box_x1, box_y1, true, true);
                    DrawMiniString3x5(d, kBottomLetters[i], value_x, value_y, false);
                }
                else
                {
                    DrawDottedRect(d, box_x0, box_y0, box_x1, box_y1, true);
                    DrawMiniString3x5(d, kBottomLetters[i], value_x, value_y, true);
                }
                continue;
            }
            DrawMiniString3x5(d, kBottomLetters[i], value_x, value_y, true);
            continue;
        }

        if(loop_stage_editing && !type_focused && stage_focus == static_cast<uint8_t>(i))
        {
            char value_buf[6] = {};
            const uint16_t value = PerformAdsrStageValue(app, layer, static_cast<uint8_t>(i));
            std::snprintf(value_buf, sizeof(value_buf), "%u", static_cast<unsigned>(value));
            const int value_w = MiniString3x5Width(value_buf);
            const int box_w = value_w + 6;
            int box_x0 = seg_center - (box_w / 2);
            int box_x1 = box_x0 + box_w - 1;
            if(box_x0 < seg_start)
            {
                box_x0 = seg_start;
                box_x1 = box_x0 + box_w - 1;
            }
            if(box_x1 > seg_end)
            {
                box_x1 = seg_end;
                box_x0 = box_x1 - box_w + 1;
            }

            const int box_y0 = bottom_y - 2;
            const int box_y1 = bottom_y + kMini3x5H + 1;
            const int value_x = box_x0 + ((box_w - value_w) / 2);
            const int value_y = bottom_y;
            DrawDottedRect(d, box_x0, box_y0, box_x1, box_y1, true);
            DrawMiniString3x5(d, value_buf, value_x, value_y, true);
        }
        else
        {
            const int w = MiniString3x5Width(kBottomLetters[i]);
            int x = seg_center - (w / 2);
            if(x < seg_start)
                x = seg_start;
            if(x + w - 1 > seg_end)
                x = seg_end - w + 1;
            if(stage_enabled && !type_focused && stage_focus == static_cast<uint8_t>(i))
            {
                d.DrawRect(x - 2, bottom_y - 1, x + w + 1, bottom_y + kMini3x5H, true, true);
                DrawMiniString3x5(d, kBottomLetters[i], x, bottom_y, false);
                continue;
            }
            DrawMiniString3x5(d, kBottomLetters[i], x, bottom_y, true);
            if(!stage_enabled)
            {
                int line_x0 = x - 1;
                int line_x1 = x + w;
                if(line_x0 < seg_start)
                    line_x0 = seg_start;
                if(line_x1 > seg_end)
                    line_x1 = seg_end;
                d.DrawLine(line_x0, bottom_y + 2, line_x1, bottom_y + 2, true);
            }
        }
    }
}
