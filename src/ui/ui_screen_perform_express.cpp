#include "ui_screens_internal.h"

#include <cstdio>

#include "app_state_diagnostics.h"
#include "app_state_engine.h"
#include "app_state_project.h"
#include "app_state_recording.h"
#include "app_state_shared.h"
#include "app_state_ui.h"
#include "app_state_worker.h"
#include "oled_pager.h"
#include "ui_draw_controls.h"
#include "ui_input.h"

namespace
{
static constexpr uint8_t kExpressRows = AppEngineState::PerformExpressState::kRowCount;
static constexpr uint8_t kExpressFocusCount = kExpressRows * 3u;

enum ExpressTarget : uint8_t
{
    kExpressCutoff = 0,
    kExpressDrive,
    kExpressResonance,
    kExpressAttack,
    kExpressSustain,
    kExpressRelease,
    kExpressReverb,
    kExpressTargetCount,
};

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static uint8_t ClampTarget(uint8_t target)
{
    return (target < kExpressTargetCount) ? target : kExpressCutoff;
}

static const char* TargetLabel(uint8_t target)
{
    switch(ClampTarget(target))
    {
        case kExpressCutoff: return "cutoff";
        case kExpressDrive: return "drive";
        case kExpressResonance: return "reso";
        case kExpressAttack: return "attack";
        case kExpressSustain: return "sustain";
        case kExpressRelease: return "release";
        case kExpressReverb: return "reverb";
        default: return "cutoff";
    }
}

static uint16_t TargetMin(uint8_t target)
{
    switch(ClampTarget(target))
    {
        case kExpressCutoff: return 20u;
        case kExpressDrive: return 0u;
        case kExpressResonance: return 0u;
        case kExpressAttack: return 2u;
        case kExpressSustain: return 0u;
        case kExpressRelease: return 1u;
        case kExpressReverb: return 0u;
        default: return 0u;
    }
}

static uint16_t TargetMax(uint8_t target)
{
    switch(ClampTarget(target))
    {
        case kExpressCutoff: return 20000u;
        case kExpressDrive: return 60u;
        case kExpressResonance: return 100u;
        case kExpressAttack: return 1000u;
        case kExpressSustain: return 100u;
        case kExpressRelease: return 1000u;
        case kExpressReverb: return 100u;
        default: return 100u;
    }
}

static int TargetStep(uint8_t target)
{
    switch(ClampTarget(target))
    {
        case kExpressCutoff: return 100;
        case kExpressDrive: return 1;
        case kExpressAttack: return 5;
        case kExpressRelease: return 5;
        default: return 1;
    }
}

static void FormatTargetValue(uint8_t target, uint16_t value, char* out, size_t out_n)
{
    switch(ClampTarget(target))
    {
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

static void NormalizeRow(AppEngineState::PerformExpressState& express, uint8_t layer, uint8_t row)
{
    layer &= 1u;
    row %= kExpressRows;
    uint8_t& target = express.target[layer][row];
    target = ClampTarget(target);

    const uint16_t lo = TargetMin(target);
    const uint16_t hi = TargetMax(target);
    uint16_t& min_v = express.min_value[layer][row];
    uint16_t& max_v = express.max_value[layer][row];

    min_v = static_cast<uint16_t>(ClampInt(static_cast<int>(min_v), lo, hi));
    max_v = static_cast<uint16_t>(ClampInt(static_cast<int>(max_v), lo, hi));
    if(min_v > max_v)
    {
        min_v = lo;
        max_v = hi;
    }
}

static void SetTarget(AppEngineState::PerformExpressState& express, uint8_t layer, uint8_t row, uint8_t next_target)
{
    layer &= 1u;
    row %= kExpressRows;
    express.target[layer][row] = ClampTarget(next_target);
    NormalizeRow(express, layer, row);
}

static void DrawHeader(OledPager& d, const char* label)
{
    d.Fill(false);
    const int box_h = kMicroH + 4;
    const int w = MicroStringWidth(label);
    const int bw = w + 4;
    const int bx = 128 - bw;
    d.DrawRect(bx, 0, bx + bw - 1, box_h - 1, true, true);
    DrawMicroString(d, label, bx + 2, 2, false);
}

static void DrawCenteredTiny(OledPager& d, const char* str, int cx, int y, bool focused)
{
    const int w = TinyStringWidth(str);
    int x = cx - w / 2;
    x = ClampInt(x, 0, 127 - w);
    if(focused)
        DrawRencFocusFrame(d, x, y, w, Font5x7::H);
    DrawTinyString(d, str, x, y, !focused);
}

static void DrawCenteredMicroInRect(OledPager& d, const char* str, int x0, int y0, int x1, int y1, bool focused)
{
    if(focused)
        d.DrawRect(x0 - 2, y0 - 2, x1 + 2, y1 + 2, true, false);
    d.DrawRect(x0, y0, x1, y1, true, false);
    const int w = MicroStringWidth(str);
    const int x = ClampInt((x0 + x1 + 1 - w) / 2, x0 + 1, x1 - w);
    const int y = y0 + ((y1 - y0 + 1) - kMicroH) / 2;
    DrawMicroString(d, str, x, y, true);
}

static void DrawLayerBadge(OledPager& d, uint8_t layer, bool inverted)
{
    const char* label = (layer & 1u) ? "B" : "A";
    d.DrawRect(0, 0, 10, 10, true, inverted);
    DrawTinyString(d, label, 3, 2, !inverted);
}
} // namespace

void PerformExpress_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine)
        return;

    AppEngineState& engine = *ctx.engine;
    for(uint8_t layer = 0; layer < AppEngineState::PerformExpressState::kLayerCount; ++layer)
    {
        for(uint8_t row = 0; row < kExpressRows; ++row)
            NormalizeRow(engine.express, layer, row);
    }
    engine.perform_nav.perform_express_focus %= kExpressFocusCount;
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
    focus %= kExpressFocusCount;
    const uint8_t row = focus / 3u;
    const uint8_t col = focus % 3u;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        int next = static_cast<int>(focus) + e.value;
        while(next < 0)
            next += kExpressFocusCount;
        while(next >= kExpressFocusCount)
            next -= kExpressFocusCount;
        focus = static_cast<uint8_t>(next);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        if(col == 1u)
        {
            int next = static_cast<int>(engine.express.target[layer][row]) + e.value;
            while(next < 0)
                next += kExpressTargetCount;
            while(next >= kExpressTargetCount)
                next -= kExpressTargetCount;
            SetTarget(engine.express, layer, row, static_cast<uint8_t>(next));
            ui.ui_dirty = true;
            return true;
        }

        NormalizeRow(engine.express, layer, row);
        const uint8_t target = engine.express.target[layer][row];
        const int lo = static_cast<int>(TargetMin(target));
        const int hi = static_cast<int>(TargetMax(target));
        const int delta = e.value * TargetStep(target);

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
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        engine.perform_nav.perform_layer ^= 1u;
        const uint8_t next_layer = engine.perform_nav.perform_layer & 1u;
        shared.sample.publish.sd_current_slot.store(next_layer, std::memory_order_release);
        engine.layer.engine_header_invert_until_ms = e.t_ms + 250u;
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
    DrawHeader(d, "express");

    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    const bool badge_invert = ctx.now_ms < engine.layer.engine_header_invert_until_ms;
    DrawLayerBadge(d, layer, badge_invert);

    engine.perform_nav.perform_express_focus %= kExpressFocusCount;
    const uint8_t focus = engine.perform_nav.perform_express_focus;

    constexpr int kRectX0 = 42;
    constexpr int kRectX1 = 86;
    constexpr int kValueLeftCx = 20;
    constexpr int kValueRightCx = 108;
    constexpr int kTopY = 18;
    constexpr int kGapY = 14;

    for(uint8_t row = 0; row < kExpressRows; ++row)
    {
        NormalizeRow(engine.express, layer, row);
        const uint8_t target = engine.express.target[layer][row];
        const int cy = kTopY + row * kGapY;
        const int y0 = cy - 5;
        const int y1 = cy + 5;
        const uint8_t base_focus = row * 3u;

        char min_buf[12] = {};
        char max_buf[12] = {};
        FormatTargetValue(target, engine.express.min_value[layer][row], min_buf, sizeof(min_buf));
        FormatTargetValue(target, engine.express.max_value[layer][row], max_buf, sizeof(max_buf));

        DrawCenteredTiny(d, min_buf, kValueLeftCx, cy - 3, focus == base_focus);
        DrawCenteredMicroInRect(d,
                                TargetLabel(target),
                                kRectX0,
                                y0,
                                kRectX1,
                                y1,
                                focus == static_cast<uint8_t>(base_focus + 1u));
        DrawCenteredTiny(d, max_buf, kValueRightCx, cy - 3, focus == static_cast<uint8_t>(base_focus + 2u));

        d.DrawLine(32, cy, kRectX0 - 1, cy, true);
        d.DrawLine(kRectX1 + 1, cy, 96, cy, true);
    }
}
