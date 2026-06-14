#include "ui_screens_internal.h"

#include <cstdio>

#include "app_state_diagnostics.h"
#include "app_state_engine.h"
#include "app_state_ui.h"
#include "oled_pager.h"
#include "ui_input.h"

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo) return lo;
    if(v > hi) return hi;
    return v;
}

// ── Shared header / layout (oled_ui_sim ui_ref/ui_screens.cpp) ───────────────

static void SubScreen_RenderHeader(OledPager& d, const char* label, const char* label2 = nullptr)
{
    d.Fill(false);
    const int box_h = kMicroH + 4;

    const int w1  = MicroStringWidth(label);
    const int bw1 = w1 + 4;
    const int bx1 = 128 - bw1;
    d.DrawRect(bx1, 0, bx1 + bw1 - 1, box_h - 1, true, true);
    DrawMicroString(d, label, bx1 + 2, 2, false);

    if(label2)
    {
        const int w2  = MicroStringWidth(label2);
        const int bw2 = w2 + 4;
        const int bx2 = 128 - bw2;
        const int by2 = box_h;
        d.DrawRect(bx2, by2, bx2 + bw2 - 1, by2 + box_h - 1, true, true);
        DrawMicroString(d, label2, bx2 + 2, by2 + 2, false);
    }
}

static void DrawDottedRect(OledPager& d, int x0, int y0, int x1, int y1)
{
    int phase = 0;
    for(int x = x0; x <= x1; ++x)
    {
        if(phase % 2 == 0)
        {
            d.DrawPixel(x, y0, true);
            d.DrawPixel(x, y1, true);
        }
        ++phase;
    }
    phase = 1;
    for(int y = y0 + 1; y <= y1 - 1; ++y)
    {
        if(phase % 2 == 0)
        {
            d.DrawPixel(x0, y, true);
            d.DrawPixel(x1, y, true);
        }
        ++phase;
    }
}

static void DrawVelModItem(OledPager& d, const char* str, int cx, int y, bool focused, bool dotted = false)
{
    const int w = MicroStringWidth(str);
    const int x = ClampInt(cx - w / 2, 1, 127 - w);
    if(focused)
    {
        if(dotted)
            DrawDottedRect(d, x - 2, y - 2, x + w + 1, y + kMicroH + 1);
        else
            d.DrawRect(x - 2, y - 2, x + w + 1, y + kMicroH + 1, true, false);
    }
    DrawMicroString(d, str, x, y, true);
}

// 5×7 tiny font for numeric (and "off") values — full digit shapes vs 6-row micro downsample.
static void DrawVelModNumeric(OledPager& d, const char* str, int cx, int y, bool focused, bool dotted = false)
{
    const int w = TinyStringWidth(str);
    const int x = ClampInt(cx - w / 2, 1, 127 - w);
    constexpr int kNumH = Font5x7::H;
    if(focused)
    {
        if(dotted)
            DrawDottedRect(d, x - 2, y - 2, x + w + 1, y + kNumH + 1);
        else
            d.DrawRect(x - 2, y - 2, x + w + 1, y + kNumH + 1, true, false);
    }
    DrawTinyString(d, str, x, y, true);
}

// Shared target list for both lanes. Index 0 = "----" (lane gated). Indices
// 5/6/7 are the send-style targets (unipolar amount 0..+10). The other
// modifying targets (1..4) are bipolar (-10..+10). Mutually exclusive across
// lanes: target encoder scroll on one lane skips the index the other lane
// is currently using (unless that index is 0/----).
static const char* const kVelModTargetList[] = {
    "----",
    "volume",
    "attack",
    "sustain",
    "release",
    "reverb",
    "delay",
    "sat",
};
static constexpr uint8_t kVelModTargetCount = 8u;
static constexpr uint8_t kVelModTargetNone  = 0u;
static constexpr uint8_t kVelModTargetFirstSend = 5u;

static bool VelModTargetIsSend(uint8_t target_idx)
{
    return target_idx >= kVelModTargetFirstSend && target_idx < kVelModTargetCount;
}

static int VelModAmountLo(uint8_t target_idx)
{
    return VelModTargetIsSend(target_idx) ? 0 : -10;
}

static int VelModAmountHi(uint8_t /*target_idx*/)
{
    return 10;
}

// Shape: 0=knee, 1=gate. The label string is what appears in the value
// column on the shape row.
static const char* VelModShapeLabel(uint8_t shape)
{
    return (shape == 0u) ? "knee" : "gate";
}

static void FormatVelocityMonitorString(char buf[4], const AppDiagnosticsState& diag)
{
    uint32_t v = diag.last_velocity.load(std::memory_order_relaxed);
    if(v > 127u)
        v = 127u;
    std::snprintf(buf, 4u, "%u", static_cast<unsigned>(v));
}

// Top-to-bottom visual order for LEnc focus scroll on the velmod screens.
// Focus IDs are 1=threshold, 2=amount, 3=target, 4=shape (historical from the
// modblock screens that share the same focus field), but the rows render in
// the order target → threshold → amount → shape. Scroll walks this table so
// the highlight follows the user's eye instead of the numeric order.
static constexpr uint8_t kVelModFocusVisualOrder[4] = {3u, 1u, 2u, 4u};

static uint8_t VelModStepFocusVisual(uint8_t cur, int dir)
{
    int cur_ord = 0;
    for(int i = 0; i < 4; ++i)
    {
        if(kVelModFocusVisualOrder[i] == cur)
        {
            cur_ord = i;
            break;
        }
    }
    const int next_ord = (dir > 0) ? (cur_ord + 1) % 4 : (cur_ord + 3) % 4;
    return kVelModFocusVisualOrder[next_ord];
}

// Walk forward/backward through the shared target list skipping whatever
// the other lane is currently using (except the always-available "----"
// at index 0). Wraps around. Returns the next valid index.
static uint8_t VelModNextTargetIdx(uint8_t cur, uint8_t other_lane_idx, int dir)
{
    const int step = (dir > 0) ? 1 : -1;
    uint8_t   next = cur;
    for(uint8_t i = 0; i < kVelModTargetCount; ++i)
    {
        next = static_cast<uint8_t>((next + step + kVelModTargetCount) % kVelModTargetCount);
        if(next == kVelModTargetNone || next != other_lane_idx)
            return next;
    }
    return cur;
}

static bool VelocityMod_HandleEvent(AppUiState& ui, AppEngineState& engine, int idx, const UiInputEvent& e)
{
    const uint8_t focus = ui.velmod_focus[idx];
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const int delta = e.value;
        if(focus == 1)
        {
            auto clamp_apply = [&](uint8_t& v) {
                int n = (int)v + delta;
                v     = (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
            };
            if(engine.velmod.threshold_linked)
            {
                clamp_apply(engine.velmod.threshold[0]);
                clamp_apply(engine.velmod.threshold[1]);
            }
            else
            {
                clamp_apply(engine.velmod.threshold[idx]);
            }
            ui.ui_dirty = true;
            return true;
        }
        if(focus == 2)
        {
            const int lo = VelModAmountLo(engine.velmod.target_idx[idx]);
            const int hi = VelModAmountHi(engine.velmod.target_idx[idx]);
            int n = (int)engine.velmod.amount[idx] + delta;
            if(n < lo) n = lo;
            if(n > hi) n = hi;
            engine.velmod.amount[idx] = static_cast<int8_t>(n);
            ui.ui_dirty = true;
            return true;
        }
        if(focus == 3)
        {
            // Skip the index the other lane currently uses (except 0/----).
            const uint8_t other = engine.velmod.target_idx[idx ^ 1];
            const uint8_t prev  = engine.velmod.target_idx[idx];
            const uint8_t next  = VelModNextTargetIdx(prev, other, delta);
            if(next != prev)
            {
                engine.velmod.target_idx[idx] = next;
                // Per spec: amount snaps to 0 whenever target changes so the
                // user always sees a known-safe starting state on a new pick.
                engine.velmod.amount[idx] = 0;
                ui.ui_dirty = true;
            }
            return true;
        }
        if(focus == 4)
        {
            // Shape toggles between knee (0) and gate (1) on encoder scroll.
            // Direction doesn't matter — it's a 2-state switch.
            engine.velmod.shape[idx] = (engine.velmod.shape[idx] == 0u) ? 1u : 0u;
            ui.ui_dirty = true;
            return true;
        }
        return false;
    }
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc && focus == 1 && ui.ui_rshift_held)
    {
        engine.velmod.threshold_linked = !engine.velmod.threshold_linked;
        ui.ui_dirty = true;
        return true;
    }
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc && focus == 4)
    {
        // REnc Click on the shape row also toggles knee/gate (matches the
        // "click to flip" feel users get on other toggle-style rows).
        engine.velmod.shape[idx] = (engine.velmod.shape[idx] == 0u) ? 1u : 0u;
        ui.ui_dirty = true;
        return true;
    }
    return false;
}

static void ModBlock_RenderCommon(OledPager& d,
                                  AppUiState& ui,
                                  AppEngineState& engine,
                                  const AppDiagnosticsState& diag,
                                  int idx,
                                  const char* header_label,
                                  bool rshift)
{
    SubScreen_RenderHeader(d, header_label);

    const uint8_t focus = ui.velmod_focus[idx];

    auto draw_lbl = [&](int cx, int y, const char* str) {
        if(!str) return;
        const int w = MicroStringWidth(str);
        const int x = ClampInt(cx - w / 2, 1, 127 - w);
        DrawMicroString(d, str, x, y, true);
    };

    DrawMicroString(d, "velocity", 5, 2, true);
    DrawMicroString(d, "monitor", 5, 10, true);
    draw_lbl(30, 30, "velocity");
    draw_lbl(30, 38, "threshold");
    draw_lbl(72, 20, "send");
    draw_lbl(72, 28, "amount");
    draw_lbl(112, 30, "target");

    {
        char vel_mon[4] = {};
        FormatVelocityMonitorString(vel_mon, diag);
        DrawVelModNumeric(d, vel_mon, 16, 18, false);
    }

    {
        constexpr int kCx = 30, kY = 48;
        const bool    focused = (focus == 1);
        if(engine.velmod.modblock_threshold_off[idx])
        {
            static const char kOff[] = "off";
            const int         w = TinyStringWidth(kOff);
            const int         x = ClampInt(kCx - w / 2, 1, 127 - w);
            if(focused)
            {
                d.DrawRect(x - 1, kY - 1, x + w, kY + Font5x7::H, true, true);
                DrawTinyString(d, kOff, x, kY, false);
            }
            else
            {
                DrawTinyString(d, kOff, x, kY, true);
            }
        }
        else
        {
            char buf[4] = {};
            std::snprintf(buf, sizeof(buf), "%u", engine.velmod.threshold[idx]);
            const int  w = TinyStringWidth(buf);
            const int  x = ClampInt(kCx - w / 2, 1, 127 - w);
            const bool inverted = focused && rshift;
            if(inverted)
            {
                DrawRencFocusTinyString(d, buf, x, kY);
            }
            else
            {
                DrawVelModNumeric(d, buf, kCx, kY, focused, true);
            }
        }
    }

    {
        char buf[6] = {};
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(engine.velmod.amount[idx]));
        DrawVelModNumeric(d, buf, 72, 40, focus == 2);
    }

    DrawVelModItem(d, kVelModTargetList[engine.velmod.target_idx[idx]], 112, 43, focus == 3);
}

static void VelocityMod_RenderCommon(OledPager& d,
                                     AppUiState& ui,
                                     AppEngineState& engine,
                                     const AppDiagnosticsState& diag,
                                     int idx,
                                     const char* header,
                                     const char* header2,
                                     bool rshift)
{
    SubScreen_RenderHeader(d, header, header2);
    const uint8_t focus = ui.velmod_focus[idx];

    // Left-side parameter list: threshold / amount / target.
    // Row pitch = 12 px keeps a 1-px gap between focus borders so the
    // tiny-font box (height 11) on rows 0/1 and the micro box (height 10)
    // on row 2 never overlap. Labels live at the left edge in micro font;
    // their value sits centered around kLeftValueCx so it can grow
    // 1..3 chars wide without disturbing the label or the link badge.
    constexpr int kLeftLabelX  = 2;
    constexpr int kLeftValueCx = 56;
    constexpr int kLinkCx      = 76;
    constexpr int kRow0Y       = 6;   // target label + value
    constexpr int kRow1Y       = 18;  // threshold + value + link
    constexpr int kRow2Y       = 30;  // amount + value
    constexpr int kRow3Y       = 42;  // shape + value

    DrawMicroString(d, "target",    kLeftLabelX, kRow0Y, true);
    DrawMicroString(d, "threshold", kLeftLabelX, kRow1Y, true);
    DrawMicroString(d, "amount",    kLeftLabelX, kRow2Y, true);
    DrawMicroString(d, "shape",     kLeftLabelX, kRow3Y, true);

    // Target value (row 0) — micro-font label string from shared list.
    DrawVelModItem(d,
                   kVelModTargetList[engine.velmod.target_idx[idx]],
                   kLeftValueCx,
                   kRow0Y,
                   focus == 3);

    // Threshold value (row 1).
    {
        char       buf[4] = {};
        std::snprintf(buf, sizeof(buf), "%u", engine.velmod.threshold[idx]);
        const int  w        = TinyStringWidth(buf);
        const int  x        = ClampInt(kLeftValueCx - w / 2, 1, 127 - w);
        const bool focused  = (focus == 1);
        const bool inverted = focused && rshift;
        if(inverted)
        {
            DrawRencFocusTinyString(d, buf, x, kRow1Y);
        }
        else
        {
            DrawVelModNumeric(d, buf, kLeftValueCx, kRow1Y, focused, true);
        }
    }

    // "link" badge on row 1 to the right of the threshold value.
    // Shown only while threshold_linked is true; RShift+REnc Click on
    // threshold toggles it. Same flag drives both first and second
    // velmod screens, so the badge appears/disappears in sync.
    if(engine.velmod.threshold_linked)
    {
        static const char lbl[] = "link";
        const int         w = MiniString3x5Width(lbl);
        const int         x = ClampInt(kLinkCx - w / 2, 1, 127 - w);
        DrawMiniString3x5(d, lbl, x, kRow1Y, true);
    }

    // Amount value (row 2). Signed format covers the bipolar -10..+10 case;
    // unipolar targets only ever scroll into 0..+10 (clamped by the event
    // handler) so the leading '-' just never appears.
    {
        char buf[6] = {};
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(engine.velmod.amount[idx]));
        DrawVelModNumeric(d, buf, kLeftValueCx, kRow2Y, focus == 2);
    }

    // Shape value (row 3) — toggle between "knee" and "gate" on REnc Click.
    DrawVelModItem(d,
                   VelModShapeLabel(engine.velmod.shape[idx]),
                   kLeftValueCx,
                   kRow3Y,
                   focus == 4);

    // Right side: monitor moves to the slot the target used to live in.
    // Label centered at cx=112 / y=30 (where "target" label sat), value
    // centered at cx=112 / y=40 (where the target value sat). Monitor is
    // read-only — no focus border needed.
    {
        char vel_mon[4] = {};
        FormatVelocityMonitorString(vel_mon, diag);
        const char* lbl = "monitor";
        const int   lw  = MicroStringWidth(lbl);
        const int   lx  = ClampInt(112 - lw / 2, 1, 127 - lw);
        DrawMicroString(d, lbl, lx, 30, true);
        DrawVelModNumeric(d, vel_mon, 112, 40, false);
    }
}

void ModBlockA_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine || !ctx.display)
        return;
    if(!ctx.diag)
        return;
    ModBlock_RenderCommon(*ctx.display, *ctx.ui, *ctx.engine, *ctx.diag, 0, "mod block a", ctx.rshift);
}

void ModBlockB_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine || !ctx.display)
        return;
    if(!ctx.diag)
        return;
    ModBlock_RenderCommon(*ctx.display, *ctx.ui, *ctx.engine, *ctx.diag, 1, "mod block b", ctx.rshift);
}

void VelocityMod_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine || !ctx.display)
        return;
    if(!ctx.diag)
        return;
    VelocityMod_RenderCommon(*ctx.display, *ctx.ui, *ctx.engine, *ctx.diag, 0, "vel mod", "first", ctx.rshift);
}

void VelocityMod2_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine || !ctx.display)
        return;
    if(!ctx.diag)
        return;
    VelocityMod_RenderCommon(*ctx.display, *ctx.ui, *ctx.engine, *ctx.diag, 1, "vel mod", "second", ctx.rshift);
}

bool ModBlockA_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.engine)
        return false;
    AppUiState&     ui     = *ctx.ui;
    AppEngineState& engine = *ctx.engine;

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc && ui.velmod_focus[0] == 1)
    {
        if(engine.velmod.modblock_threshold_off[0])
            engine.velmod.modblock_threshold_off[0] = false;
        else if(ui.ui_rshift_held)
            engine.velmod.modblock_threshold_off[0] = true;
        else
            goto velmod_handle;
        ui.ui_dirty = true;
        return true;
    }
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && ui.velmod_focus[0] == 1
       && engine.velmod.modblock_threshold_off[0])
        return false;
velmod_handle:
    if(VelocityMod_HandleEvent(ui, engine, 0, e)) { PublishEngineLayerParams(ctx); return true; }
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        const uint8_t cur = (ui.velmod_focus[0] >= 1 && ui.velmod_focus[0] <= 3) ? ui.velmod_focus[0] - 1u : 1u;
        ui.velmod_focus[0] = (e.value > 0) ? (cur + 1u) % 3u + 1u : (cur + 2u) % 3u + 1u;
        ui.ui_dirty = true;
        return true;
    }
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        UiNav_Pop(ui.ui_nav);
        UiNav_Push(ui.ui_nav, UiScreenId::ModBlockB);
        ui.ui_dirty = true;
        return true;
    }
    return false;
}

bool ModBlockB_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.engine)
        return false;
    AppUiState&     ui     = *ctx.ui;
    AppEngineState& engine = *ctx.engine;

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc && ui.velmod_focus[1] == 1)
    {
        if(engine.velmod.modblock_threshold_off[1])
            engine.velmod.modblock_threshold_off[1] = false;
        else if(ui.ui_rshift_held)
            engine.velmod.modblock_threshold_off[1] = true;
        else
            goto velmod_handle;
        ui.ui_dirty = true;
        return true;
    }
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && ui.velmod_focus[1] == 1
       && engine.velmod.modblock_threshold_off[1])
        return false;
velmod_handle:
    if(VelocityMod_HandleEvent(ui, engine, 1, e)) { PublishEngineLayerParams(ctx); return true; }
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        const uint8_t cur = (ui.velmod_focus[1] >= 1 && ui.velmod_focus[1] <= 3) ? ui.velmod_focus[1] - 1u : 1u;
        ui.velmod_focus[1] = (e.value > 0) ? (cur + 1u) % 3u + 1u : (cur + 2u) % 3u + 1u;
        ui.ui_dirty = true;
        return true;
    }
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        UiNav_Pop(ui.ui_nav);
        UiNav_Push(ui.ui_nav, UiScreenId::ModBlockA);
        ui.ui_dirty = true;
        return true;
    }
    return false;
}

bool VelocityMod_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.engine)
        return false;
    AppUiState&     ui     = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    if(VelocityMod_HandleEvent(ui, engine, 0, e)) { PublishEngineLayerParams(ctx); return true; }
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.velmod_focus[0] = VelModStepFocusVisual(ui.velmod_focus[0], e.value);
        ui.ui_dirty = true;
        return true;
    }
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        UiNav_Pop(ui.ui_nav);
        UiNav_Push(ui.ui_nav, UiScreenId::VelocityMod2);
        ui.ui_dirty = true;
        return true;
    }
    return false;
}

bool VelocityMod2_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.engine)
        return false;
    AppUiState&     ui     = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    if(VelocityMod_HandleEvent(ui, engine, 1, e)) { PublishEngineLayerParams(ctx); return true; }
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.velmod_focus[1] = VelModStepFocusVisual(ui.velmod_focus[1], e.value);
        ui.ui_dirty = true;
        return true;
    }
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        UiNav_Pop(ui.ui_nav);
        UiNav_Push(ui.ui_nav, UiScreenId::VelocityMod);
        ui.ui_dirty = true;
        return true;
    }
    return false;
}
