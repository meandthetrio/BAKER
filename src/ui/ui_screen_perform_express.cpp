#include "ui_screens_internal.h"

#include <cstdio>

#include "app_state_diagnostics.h"
#include "app_state_engine.h"
#include "app_state_project.h"
#include "app_state_recording.h"
#include "app_state_shared.h"
#include "app_state_ui.h"
#include "app_state_worker.h"
#include "express_state.h"
#include "oled_pager.h"
#include "ui_draw_controls.h"
#include "ui_input.h"

namespace
{
static constexpr uint8_t kExpressRows = kExpressRowCount;
static constexpr uint8_t kPolyPortoDetailFieldCount = 5u;

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

// REnc value-edit acceleration, mirroring the ADSR screen. A fast spin returns a
// larger tier factor so the per-target step (e.g. attack/release's 5 ms base)
// scales up to cover the full range quickly; slow clicks stay at the base step.
// Called once per range-edit event so the static timestamp tracks real
// inter-detent timing (event t_ms is the capture time, immune to queue batching).
static int ExpressAccelFactor(uint32_t t_ms)
{
    static uint32_t s_last_ms = 0;
    const uint32_t dt = t_ms - s_last_ms; // unsigned; first call -> huge -> base
    s_last_ms = t_ms;
    if(dt <= 22u)
        return 20; // rapid spin: 5 ms base -> 100 ms/detent for attack/release
    if(dt <= 55u)
        return 4; // moderate
    return 1;     // fine / single clicks
}

static void FormatTargetValue(uint8_t target, uint16_t value, char* out, size_t out_n)
{
    switch(ExpressClampTarget(target))
    {
        case kExpressNone:
            std::snprintf(out, out_n, "%s", "");
            break;
        case kExpressPolyPorto:
            std::snprintf(out, out_n, "%s", (value == 0u) ? "off" : "on");
            break;
        case kExpressCutoff:
        {
            const uint32_t cutoff_hz = static_cast<uint32_t>(value);
            if(cutoff_hz >= 1000u)
            {
                if((cutoff_hz % 1000u) == 0u)
                    std::snprintf(out, out_n, "%luk", (unsigned long)(cutoff_hz / 1000u));
                else
                    std::snprintf(out,
                                  out_n,
                                  "%lu.%01luk",
                                  (unsigned long)(cutoff_hz / 1000u),
                                  (unsigned long)((cutoff_hz % 1000u) / 100u));
            }
            else
            {
                std::snprintf(out, out_n, "%lu", (unsigned long)cutoff_hz);
            }
            break;
        }
        case kExpressDrive:
            std::snprintf(out, out_n, "%u.%u",
                          static_cast<unsigned>(value / 10u),
                          static_cast<unsigned>(value % 10u));
            break;
        case kExpressAttack:
        case kExpressRelease:
            std::snprintf(out, out_n, "%ums", static_cast<unsigned>(value));
            break;
        case kExpressResonance:
        case kExpressSustain:
        case kExpressReverb:
        default:
            std::snprintf(out, out_n, "%u", static_cast<unsigned>(value));
            break;
    }
}

static bool RowUsesFixedToggleLabels(const AppEngineState::PerformExpressState& express,
                                     uint8_t layer,
                                     uint8_t row)
{
    return ExpressTargetIsPolyPorto(express.target[layer & 1u][row % kExpressRows]);
}

static bool RowHidesSideValues(const AppEngineState::PerformExpressState& express,
                               uint8_t layer,
                               uint8_t row)
{
    return ExpressTargetIsNone(express.target[layer & 1u][row % kExpressRows]);
}

static bool RowSkipsSideValueFocus(const AppEngineState::PerformExpressState& express,
                                   uint8_t layer,
                                   uint8_t row)
{
    return RowUsesFixedToggleLabels(express, layer, row) || RowHidesSideValues(express, layer, row);
}

static void NormalizeRow(AppEngineState::PerformExpressState& express, uint8_t layer, uint8_t row)
{
    layer &= 1u;
    row %= kExpressRows;
    uint8_t& target = express.target[layer][row];
    uint16_t& min_v = express.min_value[layer][row];
    uint16_t& max_v = express.max_value[layer][row];
    ExpressClampRow(target, min_v, max_v);
}

static void SetTarget(AppEngineState::PerformExpressState& express, uint8_t layer, uint8_t row, uint8_t next_target)
{
    layer &= 1u;
    row %= kExpressRows;
    const uint8_t prev_target = ExpressClampTarget(express.target[layer][row]);
    const uint8_t safe_target = ExpressClampTarget(next_target);
    express.target[layer][row] = safe_target;
    if(ExpressTargetIsNone(safe_target))
    {
        express.min_value[layer][row] = 0u;
        express.max_value[layer][row] = 0u;
    }
    else if(ExpressTargetIsPolyPorto(safe_target))
    {
        express.min_value[layer][row] = 0u;
        express.max_value[layer][row] = 1u;
    }
    else if(ExpressTargetIsPolyPorto(prev_target) || ExpressTargetIsNone(prev_target))
    {
        express.min_value[layer][row] = ExpressTargetMin(safe_target);
        express.max_value[layer][row] = ExpressTargetMax(safe_target);
    }
    NormalizeRow(express, layer, row);
}

static void DrawHeader(OledPager& d, const char* label, bool inverted)
{
    const int box_h = kMicroH + 4;
    const int w = MicroStringWidth(label);
    const int bw = w + 4;
    const int bx = 128 - bw;
    if(inverted)
    {
        d.DrawRect(bx, 0, bx + bw - 1, box_h - 1, false, true);
        d.DrawRect(bx, 0, bx + bw - 1, box_h - 1, true, false);
        DrawMicroString(d, label, bx + 2, 2, true);
        return;
    }
    d.DrawRect(bx, 0, bx + bw - 1, box_h - 1, true, true);
    DrawMicroString(d, label, bx + 2, 2, false);
}

static void DrawCenteredTiny(OledPager& d, const char* str, int cx, int y, bool focused)
{
    const int w = TinyStringWidth(str);
    int x = cx - w / 2;
    x = ClampInt(x, 0, 127 - w);
    if(focused)
    {
        d.DrawRect(x - 2, y - 2, x + w + 1, y + Font5x7::H + 1, true, false);
        DrawTinyString(d, str, x, y, true);
        return;
    }
    DrawTinyString(d, str, x, y, true);
}

static void DrawCenteredMicroTarget(OledPager& d,
                                    const char* str,
                                    int x0,
                                    int y0,
                                    int x1,
                                    int y1,
                                    bool focused,
                                    bool rshift_held,
                                    bool dotted_border)
{
    const int w = MicroStringWidth(str);
    const int x = ClampInt((x0 + x1 + 1 - w) / 2, x0 + 1, x1 - w);
    const int y = y0 + ((y1 - y0 + 1) - kMicroH) / 2;

    if(!focused)
    {
        DrawMicroString(d, str, x, y, true);
        return;
    }

    // Express target R-shift visual behavior applies only to POLYPORTO.
    if(rshift_held && dotted_border)
    {
        d.DrawRect(x0, y0, x1, y1, true, true);
        DrawMicroString(d, str, x, y, false);
        return;
    }

    if(dotted_border)
        DrawDottedRect(d, x0, y0, x1, y1, true);
    else
        d.DrawRect(x0, y0, x1, y1, true, false);

    DrawMicroString(d, str, x, y, true);
}

static void DrawCenteredMicroText(OledPager& d, const char* str, int cx, int y)
{
    const int w = MicroStringWidth(str);
    int x = cx - (w / 2);
    x = ClampInt(x, 0, 127 - w);
    DrawMicroString(d, str, x, y, true);
}

static void NormalizeExpressState(AppEngineState::PerformExpressState& express)
{
    ExpressNormalizeAssignments(express.target, express.min_value, express.max_value);
    for(uint8_t layer = 0; layer < kExpressLayerCount; ++layer)
    {
        ExpressClampPolyPortoConfig(express.poly_porto_voice_limit[layer],
                                    express.poly_porto_slide_ms[layer],
                                    express.poly_porto_source_range_semitones[layer],
                                    express.poly_porto_source_mode[layer],
                                    express.poly_porto_release_ms[layer]);
    }
}

static uint8_t NormalizeFocusableExpressFocus(const AppEngineState::PerformExpressState& express,
                                              uint8_t layer,
                                              uint8_t focus)
{
    const uint8_t safe_focus = static_cast<uint8_t>(focus % kExpressFocusCount);
    if(safe_focus == 0u)
        return 0u;
    const uint8_t row = static_cast<uint8_t>((safe_focus - 1u) / 3u);
    const uint8_t col = static_cast<uint8_t>((safe_focus - 1u) % 3u);
    if((col == 0u || col == 2u) && RowSkipsSideValueFocus(express, layer, row))
        return static_cast<uint8_t>(1u + row * 3u + 1u);
    return safe_focus;
}

static uint8_t AdvanceExpressFocus(const AppEngineState::PerformExpressState& express,
                                   uint8_t layer,
                                   uint8_t focus,
                                   int delta)
{
    if(delta == 0)
        return NormalizeFocusableExpressFocus(express, layer, focus);
    int next = static_cast<int>(NormalizeFocusableExpressFocus(express, layer, focus));
    int steps = (delta > 0) ? delta : -delta;
    const int dir = (delta > 0) ? 1 : -1;
    while(steps-- > 0)
    {
        do
        {
            next += dir;
            if(next < 0)
                next += kExpressFocusCount;
            else if(next >= kExpressFocusCount)
                next -= kExpressFocusCount;
        } while(NormalizeFocusableExpressFocus(express, layer, static_cast<uint8_t>(next))
                != static_cast<uint8_t>(next));
    }
    return static_cast<uint8_t>(next);
}

static void FormatPolyPortoDetailValue(uint8_t field,
                                       const AppEngineState::PerformExpressState& express,
                                       uint8_t layer,
                                       char* out,
                                       size_t out_n)
{
    switch(field % kPolyPortoDetailFieldCount)
    {
        case 0:
            std::snprintf(out, out_n, "%ums", static_cast<unsigned>(express.poly_porto_slide_ms[layer]));
            break;
        case 1:
        {
            const unsigned n = static_cast<unsigned>(express.poly_porto_source_range_semitones[layer]);
            const char* suffix = "th";
            if((n % 100u) < 11u || (n % 100u) > 13u)
            {
                if((n % 10u) == 1u) suffix = "st";
                else if((n % 10u) == 2u) suffix = "nd";
                else if((n % 10u) == 3u) suffix = "rd";
            }
            std::snprintf(out, out_n, "%u%s", n, suffix);
            break;
        }
        case 2:
            std::snprintf(out,
                          out_n,
                          "%s",
                          (express.poly_porto_source_mode[layer] == kExpressPolyPortoSourceLatest)
                              ? "LATEST"
                              : "CLOSE");
            break;
        case 3:
            std::snprintf(out, out_n, "%ums", static_cast<unsigned>(express.poly_porto_release_ms[layer]));
            break;
        default:
            std::snprintf(out, out_n, "%u", static_cast<unsigned>(express.poly_porto_voice_limit[layer]));
            break;
    }
}

static void DrawPolyPortoDetail(OledPager& d,
                                const AppEngineState::PerformExpressState& express,
                                uint8_t layer,
                                uint8_t field_focus)
{
    constexpr int kPanelX0 = 6;
    constexpr int kPanelY0 = 12;
    constexpr int kPanelX1 = 121;
    constexpr int kPanelY1 = 61;
    constexpr int kLabelX = 11;
    constexpr int kValueX0 = 72;
    constexpr int kValueX1 = 116;
    constexpr int kRowY[kPolyPortoDetailFieldCount] = {20, 28, 36, 44, 52};
    static const char* kLabels[kPolyPortoDetailFieldCount] = {"TIME", "RANGE", "SRC", "REL", "LIMIT"};

    d.DrawRect(kPanelX0, kPanelY0, kPanelX1, kPanelY1, true, false);
    d.DrawRect(kPanelX0 + 1, kPanelY0 + 1, kPanelX1 - 1, kPanelY1 - 1, true, false);
    (void)layer;

    for(uint8_t i = 0; i < kPolyPortoDetailFieldCount; ++i)
    {
        char value_buf[16] = {};
        FormatPolyPortoDetailValue(i, express, layer, value_buf, sizeof(value_buf));
        DrawTinyString(d, kLabels[i], kLabelX, kRowY[i], true);
        if(field_focus == i)
            d.DrawRect(kValueX0 - 1, kRowY[i] - 1, kValueX1 + 1, kRowY[i] + 7, true, false);
        DrawCenteredTiny(d, value_buf, (kValueX0 + kValueX1) / 2, kRowY[i], false);
    }
}

static void DrawBypassButton(OledPager& d, bool enabled, bool focused)
{
    const char* label = enabled ? "on" : "off";
    const int x = 2;
    const int y = 2;
    if(focused)
    {
        const int w = MicroStringWidth(label);
        d.DrawRect(x - 2, y - 2, x + w + 1, y + kMicroH + 1, true, true);
        DrawMicroString(d, label, x, y, false);
    }
    else
        DrawMicroString(d, label, x, y, true);
}
} // namespace

void PerformExpress_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine)
        return;

    AppEngineState& engine = *ctx.engine;
    NormalizeExpressState(engine.express);
    engine.express.perform_express_detail_active = false;
    engine.express.perform_express_detail_field %= kPolyPortoDetailFieldCount;
    engine.perform_nav.perform_express_focus
        = NormalizeFocusableExpressFocus(engine.express,
                                         engine.perform_nav.perform_layer & 1u,
                                         engine.perform_nav.perform_express_focus);
    ctx.ui->ui_dirty = true;
}

bool PerformExpress_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.engine || !ctx.shared)
        return false;
    if(ctx.shift)
        return false;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    uint8_t& focus = engine.perform_nav.perform_express_focus;
    NormalizeExpressState(engine.express);
    focus = NormalizeFocusableExpressFocus(engine.express, layer, focus);
    if(engine.express.perform_express_detail_active)
    {
        const uint8_t detail_row = static_cast<uint8_t>((focus > 0u) ? ((focus - 1u) / 3u) : 0u);
        if(focus == 0u || !ExpressTargetIsPolyPorto(engine.express.target[layer][detail_row])
           || focus != static_cast<uint8_t>(1u + detail_row * 3u + 1u))
        {
            engine.express.perform_express_detail_active = false;
        }
    }
    const bool bypass_focus = (focus == 0u);
    const uint8_t row = bypass_focus ? 0u : static_cast<uint8_t>((focus - 1u) / 3u);
    const uint8_t col = bypass_focus ? 0u : static_cast<uint8_t>((focus - 1u) % 3u);
    const bool polyporto_target_focus = !bypass_focus && col == 1u
                                     && ExpressTargetIsPolyPorto(engine.express.target[layer][row]);

    if(engine.express.perform_express_detail_active)
    {
        uint8_t& detail_focus = engine.express.perform_express_detail_field;
        detail_focus %= kPolyPortoDetailFieldCount;
        if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
        {
            int next = static_cast<int>(detail_focus) + e.value;
            while(next < 0)
                next += static_cast<int>(kPolyPortoDetailFieldCount);
            while(next >= static_cast<int>(kPolyPortoDetailFieldCount))
                next -= static_cast<int>(kPolyPortoDetailFieldCount);
            detail_focus = static_cast<uint8_t>(next);
            ui.ui_dirty = true;
            return true;
        }
        if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
        {
            if(detail_focus == 0u)
            {
                const int next = ClampInt(static_cast<int>(engine.express.poly_porto_slide_ms[layer])
                                              + (e.value * static_cast<int>(kExpressPolyPortoTimeStepMs)),
                                          static_cast<int>(kExpressPolyPortoTimeMinMs),
                                          static_cast<int>(kExpressPolyPortoTimeMaxMs));
                engine.express.poly_porto_slide_ms[layer] = static_cast<uint16_t>(next);
            }
            else if(detail_focus == 1u)
            {
                const int next = ClampInt(static_cast<int>(engine.express.poly_porto_source_range_semitones[layer]) + e.value,
                                          static_cast<int>(kExpressPolyPortoRangeMinSemitones),
                                          static_cast<int>(kExpressPolyPortoRangeMaxSemitones));
                engine.express.poly_porto_source_range_semitones[layer] = static_cast<uint8_t>(next);
            }
            else if(detail_focus == 2u)
            {
                const int next = ClampInt(static_cast<int>(engine.express.poly_porto_source_mode[layer]) + e.value,
                                          static_cast<int>(kExpressPolyPortoSourceClosest),
                                          static_cast<int>(kExpressPolyPortoSourceLatest));
                engine.express.poly_porto_source_mode[layer] = static_cast<uint8_t>(next);
            }
            else if(detail_focus == 3u)
            {
                const int next = ClampInt(static_cast<int>(engine.express.poly_porto_release_ms[layer])
                                              + (e.value * static_cast<int>(kExpressPolyPortoReleaseStepMs)),
                                          static_cast<int>(kExpressPolyPortoReleaseMinMs),
                                          static_cast<int>(kExpressPolyPortoReleaseMaxMs));
                engine.express.poly_porto_release_ms[layer] = static_cast<uint16_t>(next);
            }
            else
            {
                const int next = ClampInt(static_cast<int>(engine.express.poly_porto_voice_limit[layer]) + e.value,
                                          static_cast<int>(kExpressPolyPortoVoicesMin),
                                          static_cast<int>(kExpressPolyPortoVoicesMax));
                engine.express.poly_porto_voice_limit[layer] = static_cast<uint8_t>(next);
            }
            PublishEngineLayerParams(ctx);
            ui.ui_dirty = true;
            return true;
        }
        if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
        {
            engine.express.perform_express_detail_active = false;
            ui.ui_dirty = true;
            return true;
        }
        if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
            return true;
        return false;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        focus = AdvanceExpressFocus(engine.express, layer, focus, e.value);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        if(bypass_focus)
            return false;
        if(RowSkipsSideValueFocus(engine.express, layer, row) && col != 1u)
            return false;

        if(col == 1u)
        {
            const uint8_t next
                = ExpressAdvanceSelectableTarget(engine.express.target,
                                                layer,
                                                row,
                                                engine.express.target[layer][row],
                                                e.value);
            SetTarget(engine.express, layer, row, next);
            NormalizeExpressState(engine.express);
            PublishEngineLayerParams(ctx);
            ui.ui_dirty = true;
            return true;
        }

        NormalizeRow(engine.express, layer, row);
        const uint8_t target = engine.express.target[layer][row];
        const int lo = static_cast<int>(ExpressTargetMin(target));
        const int hi = static_cast<int>(ExpressTargetMax(target));
        const int delta = e.value * ExpressTargetStep(target) * ExpressAccelFactor(e.t_ms);

        if(col == 0u)
        {
            int next = static_cast<int>(engine.express.min_value[layer][row]) + delta;
            next = ClampInt(next, lo, static_cast<int>(engine.express.max_value[layer][row]));
            engine.express.min_value[layer][row] = static_cast<uint16_t>(next);
        }
        else
        {
            int next = static_cast<int>(engine.express.max_value[layer][row]) + delta;
            next = ClampInt(next, static_cast<int>(engine.express.min_value[layer][row]), hi);
            engine.express.max_value[layer][row] = static_cast<uint16_t>(next);
        }
        PublishEngineLayerParams(ctx);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc && bypass_focus)
    {
        const uint8_t next = shared.performance.express.enabled.load(std::memory_order_acquire) ? 0u : 1u;
        shared.performance.express.enabled.store(next, std::memory_order_release);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc && polyporto_target_focus && ctx.rshift)
    {
        engine.express.perform_express_detail_active = true;
        engine.express.perform_express_detail_field %= kPolyPortoDetailFieldCount;
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        // Single layer: Button 2 no longer swaps layers — no-op (consume only).
        ui.ui_dirty = true;
        return true;
    }

    return false;
}

void PerformExpress_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine || !ctx.display)
        return;

    AppEngineState& engine = *ctx.engine;
    OledPager& d = *ctx.display;
    d.Fill(false);
    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    const bool badge_invert = ctx.now_ms < engine.layer.engine_header_invert_until_ms;
    char header_label[16] = {};
    std::snprintf(header_label, sizeof(header_label), "express");
    DrawHeader(d, header_label, badge_invert);
    const bool express_enabled = ctx.shared->performance.express.enabled.load(std::memory_order_acquire) != 0u;
    DrawBypassButton(d, express_enabled, engine.perform_nav.perform_express_focus == 0u);

    NormalizeExpressState(engine.express);
    engine.perform_nav.perform_express_focus
        = NormalizeFocusableExpressFocus(engine.express, layer, engine.perform_nav.perform_express_focus);
    const uint8_t focus = engine.perform_nav.perform_express_focus;

    // PolyPorto detail is a dedicated page to keep navigation/readability clear.
    if(engine.express.perform_express_detail_active)
    {
        DrawPolyPortoDetail(d,
                            engine.express,
                            layer,
                            engine.express.perform_express_detail_field % kPolyPortoDetailFieldCount);
        return;
    }

    constexpr int kRectX0 = 42;
    constexpr int kRectX1 = 86;
    constexpr int kValueLeftCx = 20;
    constexpr int kValueRightCx = 108;
    constexpr int kTopY = 18;
    constexpr int kGapY = 14;
    constexpr int kFooterY = 56;

    for(uint8_t row = 0; row < kExpressRows; ++row)
    {
        NormalizeRow(engine.express, layer, row);
        const uint8_t target = engine.express.target[layer][row];
        const int cy = kTopY + row * kGapY;
        const int y0 = cy - 5;
        const int y1 = cy + 5;
        const uint8_t base_focus = static_cast<uint8_t>(1u + row * 3u);

        char min_buf[12] = {};
        char max_buf[12] = {};
        const bool fixed_toggle = RowUsesFixedToggleLabels(engine.express, layer, row);
        const bool hide_values = RowHidesSideValues(engine.express, layer, row);
        if(fixed_toggle)
        {
            std::snprintf(min_buf, sizeof(min_buf), "off");
            std::snprintf(max_buf, sizeof(max_buf), "on");
        }
        else if(!hide_values)
        {
            FormatTargetValue(target, engine.express.min_value[layer][row], min_buf, sizeof(min_buf));
            FormatTargetValue(target, engine.express.max_value[layer][row], max_buf, sizeof(max_buf));
        }

        if(!hide_values)
        {
            if(fixed_toggle)
            {
                const int w = TinyStringWidth(min_buf);
                int x = kValueLeftCx - (w / 2);
                x = ClampInt(x, 0, 127 - w);
                if(focus == base_focus)
                {
                    d.DrawRect(x - 3, cy - 6, x + w + 2, cy + Font5x7::H - 1, false, true);
                    DrawTinyString(d, min_buf, x, cy - 3, true);
                }
                else
                {
                    DrawTinyString(d, min_buf, x, cy - 3, true);
                }
            }
            else
            {
                DrawCenteredTiny(d,
                                 min_buf,
                                 kValueLeftCx,
                                 cy - 3,
                                 focus == base_focus);
            }
        }
        DrawCenteredMicroTarget(d,
                                ExpressTargetLabel(target),
                                kRectX0,
                                y0,
                                kRectX1,
                                y1,
                                focus == static_cast<uint8_t>(base_focus + 1u),
                                ctx.rshift,
                                ExpressTargetIsPolyPorto(target));
        if(!hide_values)
        {
            if(fixed_toggle)
            {
                const int w = TinyStringWidth(max_buf);
                int x = kValueRightCx - (w / 2);
                x = ClampInt(x, 0, 127 - w);
                if(focus == static_cast<uint8_t>(base_focus + 2u))
                {
                    d.DrawRect(x - 3, cy - 6, x + w + 2, cy + Font5x7::H - 1, false, true);
                    DrawTinyString(d, max_buf, x, cy - 3, true);
                }
                else
                {
                    DrawTinyString(d, max_buf, x, cy - 3, true);
                }
            }
            else
            {
                DrawCenteredTiny(d,
                                 max_buf,
                                 kValueRightCx,
                                 cy - 3,
                                 focus == static_cast<uint8_t>(base_focus + 2u));
            }
        }

        d.DrawLine(32, cy, kRectX0 - 1, cy, true);
        d.DrawLine(kRectX1 + 1, cy, 96, cy, true);
    }

    const int target_cx = (kRectX0 + kRectX1) / 2;
    DrawCenteredMicroText(d, "lo", kValueLeftCx, kFooterY);
    DrawCenteredMicroText(d, "midimod", target_cx, kFooterY);
    DrawCenteredMicroText(d, "hi", kValueRightCx, kFooterY);

}
