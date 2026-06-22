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

// Source: which value drives the gate + knee, and the trigger polarity.
//   0=>vel  1=<vel  2=>note  3=<note   (default >vel = legacy behavior)
static const char* const kVelModSourceLabels[] = {">vel", "<vel", ">note", "<note"};
static constexpr uint8_t kVelModSourceCount = 4u;
static constexpr int kVelModNoteLoUi      = 24;  // C1
static constexpr int kVelModNoteHiUi      = 108; // C8
static constexpr int kVelModNoteDefaultUi = 60;  // C4

static bool VelModSourceIsNote(uint8_t source)
{
    return source >= 2u;
}

// Clamp a threshold to the valid range for its source domain: note = C1..C8,
// velocity = 0..127.
static uint8_t VelModClampThreshold(int v, uint8_t source)
{
    return VelModSourceIsNote(source)
               ? static_cast<uint8_t>(ClampInt(v, kVelModNoteLoUi, kVelModNoteHiUi))
               : static_cast<uint8_t>(ClampInt(v, 0, 127));
}

// Threshold display: note name (e.g. "C4") for note domain, raw number for vel.
static void VelModFormatThreshold(char* out, size_t out_n, uint8_t threshold, uint8_t source)
{
    if(VelModSourceIsNote(source))
        FormatMidiNoteName(threshold, out, out_n);
    else
        std::snprintf(out, out_n, "%u", threshold);
}

static void FormatVelocityMonitorString(char buf[4], const AppDiagnosticsState& diag)
{
    uint32_t v = diag.last_velocity.load(std::memory_order_relaxed);
    if(v > 127u)
        v = 127u;
    std::snprintf(buf, 4u, "%u", static_cast<unsigned>(v));
}

// Top-to-bottom visual order for LEnc focus scroll on the velmod screens.
// Focus IDs are 1=threshold, 2=amount, 3=target, 4=shape, 5=source (historical
// numbering from the modblock screens that share the same focus field), but the
// rows render in the order source → target → threshold → amount → shape. Scroll
// walks this table so the highlight follows the user's eye, not the numeric order.
static constexpr uint8_t kVelModFocusCount = 5u;
static constexpr uint8_t kVelModFocusVisualOrder[kVelModFocusCount] = {5u, 3u, 1u, 2u, 4u};

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
    const uint8_t focus = ui.velmod_focus[idx];
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const int delta = e.value;
        if(focus == 5)
        {
            // Source row: cycle >vel / <vel / >note / <note. Entering the note
            // domain snaps an out-of-range threshold to C4 so the user lands on a
            // sane musical default.
            int s = static_cast<int>(engine.velmod.source[idx]) + delta;
            while(s < 0) s += static_cast<int>(kVelModSourceCount);
            while(s >= static_cast<int>(kVelModSourceCount)) s -= static_cast<int>(kVelModSourceCount);
            const uint8_t prev = engine.velmod.source[idx];
            const uint8_t next = static_cast<uint8_t>(s);
            if(next != prev)
            {
                engine.velmod.source[idx] = next;
                if(VelModSourceIsNote(next) && !VelModSourceIsNote(prev))
                {
                    const int t = static_cast<int>(engine.velmod.threshold[idx]);
                    if(t < kVelModNoteLoUi || t > kVelModNoteHiUi)
                        engine.velmod.threshold[idx] = static_cast<uint8_t>(kVelModNoteDefaultUi);
                }
                ui.ui_dirty = true;
            }
            return true;
        }
        if(focus == 1)
        {
            // Threshold edits the focused lane only (threshold-link was removed).
            engine.velmod.threshold[idx] = VelModClampThreshold(
                static_cast<int>(engine.velmod.threshold[idx]) + delta,
                engine.velmod.source[idx]);
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
            // Both lanes (A/B) gate independently on their own source, and their
            // contributions sum in the engine — so the two lanes may freely share
            // a target (e.g. reverb on the low half and the high half). Pass an
            // out-of-range "other" so the target scroll never skips an index.
            const uint8_t other = 0xFFu;
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

    // Left-side parameter list: source / target / threshold / amount / shape.
    // Row pitch = 12 px keeps a 1-px gap between focus borders. Five rows fill
    // y=6..62 on the 0..63 screen. Labels live at the left edge in micro font;
    // their value sits centered around kLeftValueCx so it can grow 1..3 chars
    // wide without disturbing the label or the link badge.
    constexpr int kLeftLabelX  = 2;
    constexpr int kLeftValueCx = 59;
    constexpr int kRow0Y       = 6;   // source label + value
    constexpr int kRow1Y       = 18;  // target label + value
    constexpr int kRow2Y       = 30;  // threshold + value + link
    constexpr int kRow3Y       = 42;  // amount + value
    constexpr int kRow4Y       = 54;  // shape + value

    const uint8_t source = engine.velmod.source[idx];

    DrawMicroString(d, "source",    kLeftLabelX, kRow0Y, true);
    DrawMicroString(d, "target",    kLeftLabelX, kRow1Y, true);
    DrawMicroString(d, "threshold", kLeftLabelX, kRow2Y, true);
    DrawMicroString(d, "amount",    kLeftLabelX, kRow3Y, true);
    DrawMicroString(d, "shape",     kLeftLabelX, kRow4Y, true);

    // Source value (row 0) — micro-font label from the 4-way list.
    DrawVelModItem(d, kVelModSourceLabels[source & 3u], kLeftValueCx, kRow0Y, focus == 5);

    // Target value (row 1) — micro-font label string from shared list.
    DrawVelModItem(d,
                   kVelModTargetList[engine.velmod.target_idx[idx]],
                   kLeftValueCx,
                   kRow1Y,
                   focus == 3);

    // Threshold value (row 2) — note name for note source, number for velocity.
    // Solid focus border (matches the other value rows); threshold-link removed.
    {
        char buf[6] = {};
        VelModFormatThreshold(buf, sizeof(buf), engine.velmod.threshold[idx], source);
        DrawVelModNumeric(d, buf, kLeftValueCx, kRow2Y, focus == 1);
    }

    // Amount value (row 3). Signed format covers the bipolar -10..+10 case;
    // unipolar targets only ever scroll into 0..+10 (clamped by the event
    // handler) so the leading '-' just never appears.
    {
        char buf[6] = {};
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(engine.velmod.amount[idx]));
        DrawVelModNumeric(d, buf, kLeftValueCx, kRow3Y, focus == 2);
    }

    // Shape value (row 4) — toggle between "knee" and "gate" on REnc Click.
    DrawVelModItem(d,
                   VelModShapeLabel(engine.velmod.shape[idx]),
                   kLeftValueCx,
                   kRow4Y,
                   focus == 4);

    // Right side: monitor in the right column (label y=30, value y=40). Shows the
    // last note name when this lane's source is note domain, else the last
    // velocity. Read-only — no focus border needed.
    {
        char mon[6] = {};
        if(VelModSourceIsNote(source))
        {
            uint32_t n = diag.last_note.load(std::memory_order_relaxed);
            if(n > 127u)
                n = 127u;
            FormatMidiNoteName(static_cast<uint8_t>(n), mon, sizeof(mon));
        }
        else
        {
            FormatVelocityMonitorString(mon, diag);
        }
        const char* lbl = "monitor";
        const int   lw  = MicroStringWidth(lbl);
        const int   lx  = ClampInt(112 - lw / 2, 1, 127 - lw);
        DrawMicroString(d, lbl, lx, 30, true);
        DrawVelModNumeric(d, mon, 112, 40, false);
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
