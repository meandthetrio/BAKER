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


// Since a/b are now automatic pitch keyzones (source/threshold/shape are driven
// by the keyzone split), the lane editor is trimmed to just target + amount.
// Focus IDs keep their historical numbering (2=amount, 3=target); scroll walks
// the visual top-to-bottom order (target over amount).
static constexpr uint8_t kVelModFocusCount = 2u;
static constexpr uint8_t kVelModFocusVisualOrder[kVelModFocusCount] = {3u, 2u};

// Clamp a stale/legacy focus value into the trimmed {target, amount} set.
static uint8_t VelModNormalizeFocus(uint8_t focus)
{
    return (focus == 2u || focus == 3u) ? focus : 3u;
}

static uint8_t VelModStepFocusVisual(uint8_t cur, int dir)
{
    int cur_ord = 0;
    for(int i = 0; i < static_cast<int>(kVelModFocusCount); ++i)
    {
        if(kVelModFocusVisualOrder[i] == cur)
        {
            cur_ord = i;
            break;
        }
    }
    const int n = static_cast<int>(kVelModFocusCount);
    const int next_ord = (dir > 0) ? (cur_ord + 1) % n : (cur_ord + n - 1) % n;
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
    const uint8_t focus = VelModNormalizeFocus(ui.velmod_focus[idx]);
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const int delta = e.value;
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
            // Both lanes may share a target (they gate on their own zone and sum
            // in the engine), so pass an out-of-range "other" that's never skipped.
            const uint8_t other = 0xFFu;
            const uint8_t prev  = engine.velmod.target_idx[idx];
            const uint8_t next  = VelModNextTargetIdx(prev, other, delta);
            if(next != prev)
            {
                engine.velmod.target_idx[idx] = next;
                // Amount snaps to 0 on a target change so a new pick starts safe.
                engine.velmod.amount[idx] = 0;
                ui.ui_dirty = true;
            }
            return true;
        }
        return false;
    }
    return false;
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
    const uint8_t focus = VelModNormalizeFocus(ui.velmod_focus[idx]);
    (void)rshift;
    (void)diag;

    // Trimmed lane editor: just target + amount (a/b are pitch keyzones whose
    // note coverage is set by the keyzone split). Each label sits centered above
    // its value.
    constexpr int kCx      = 64;  // horizontal centre for both stacks
    constexpr int kTLblY   = 14;  // target label
    constexpr int kTValY   = 24;  // target value
    constexpr int kALblY   = 40;  // amount label
    constexpr int kAValY   = 50;  // amount value

    auto draw_centered_label = [&](const char* s, int y)
    {
        const int w = MicroStringWidth(s);
        DrawMicroString(d, s, kCx - w / 2, y, true);
    };

    // Target: label above the value.
    draw_centered_label("target", kTLblY);
    DrawVelModItem(d,
                   kVelModTargetList[engine.velmod.target_idx[idx]],
                   kCx,
                   kTValY,
                   focus == 3);

    // Amount: label above the value (signed; unipolar sends never show '-').
    draw_centered_label("amount", kALblY);
    {
        char buf[6] = {};
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(engine.velmod.amount[idx]));
        DrawVelModNumeric(d, buf, kCx, kAValY, focus == 2);
    }
}

// The two keymod lanes (A=lane 0, B=lane 1) share one editor; only the top-right
// header label differs. Pod2 toggles between the two lanes.
void VelocityMod_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine || !ctx.display)
        return;
    if(!ctx.diag)
        return;
    VelocityMod_RenderCommon(*ctx.display, *ctx.ui, *ctx.engine, *ctx.diag, 0, "keymod", "lane a", ctx.rshift);
}

void VelocityMod2_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine || !ctx.display)
        return;
    if(!ctx.diag)
        return;
    VelocityMod_RenderCommon(*ctx.display, *ctx.ui, *ctx.engine, *ctx.diag, 1, "keymod", "lane b", ctx.rshift);
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
        ui.keymod_blink_ms = e.t_ms; // LED2 blinks off on the lane switch
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
        ui.keymod_blink_ms = e.t_ms; // LED2 blinks off on the lane switch
        UiNav_Pop(ui.ui_nav);
        UiNav_Push(ui.ui_nav, UiScreenId::VelocityMod);
        ui.ui_dirty = true;
        return true;
    }
    return false;
}
