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

static void DrawFourSectionLayout(OledPager& d, int sec2_cx = 72)
{
    constexpr int kHdrH     = kMicroH + 4;
    constexpr int kContentY = kHdrH;
    constexpr int kContentH = 64 - kContentY;

    const int two_h   = kMicroH + 2 + kMicroH;
    const int base_y1 = kContentY + (kContentH - two_h) / 2;
    const int base_y2 = base_y1 + kMicroH + 2;

    struct Section
    {
        int         cx;
        const char* l1;
        const char* l2;
        int         y_off;
    };
    const Section kSecs[4] = {
        {16, "velocity", "monitor", -30},
        {48, "velocity", "threshold", 0},
        {sec2_cx, "send", "amount", -30},
        {112, "target", nullptr, 0},
    };

    for(const auto& sec : kSecs)
    {
        const int y1 = base_y1 + sec.y_off;
        const int y2 = base_y2 + sec.y_off;
        if(sec.l1)
        {
            const int w = MicroStringWidth(sec.l1);
            const int x = ClampInt(sec.cx - w / 2, 1, 127 - w);
            DrawMicroString(d, sec.l1, x, y1, true);
        }
        if(sec.l2)
        {
            const int w = MicroStringWidth(sec.l2);
            const int x = ClampInt(sec.cx - w / 2, 1, 127 - w);
            DrawMicroString(d, sec.l2, x, y2, true);
        }
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

static const char* const kVelModTargets0[] = {"gain", "drive"};
static const char* const kVelModTargets1[] = {"delay", "verb"};
static const char* const* kVelModTargets[2] = {kVelModTargets0, kVelModTargets1};
static const uint8_t       kVelModTargetCounts[2] = {2, 2};

static void FormatVelocityMonitorString(char buf[4], const AppDiagnosticsState& diag)
{
    uint32_t v = diag.last_velocity.load(std::memory_order_relaxed);
    if(v > 127u)
        v = 127u;
    std::snprintf(buf, 4u, "%u", static_cast<unsigned>(v));
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
            int n = (int)engine.velmod.send_amount[idx] + delta;
            engine.velmod.send_amount[idx] = (uint8_t)(n < 0 ? 0 : n > 20 ? 20 : n);
            ui.ui_dirty = true;
            return true;
        }
        if(focus == 3)
        {
            const uint8_t count = kVelModTargetCounts[idx];
            if(delta > 0)
                engine.velmod.target_idx[idx] = (engine.velmod.target_idx[idx] + 1u) % count;
            else
                engine.velmod.target_idx[idx] = (engine.velmod.target_idx[idx] + count - 1u) % count;
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
        char buf[4] = {};
        std::snprintf(buf, sizeof(buf), "%u", engine.velmod.send_amount[idx]);
        DrawVelModNumeric(d, buf, 72, 40, focus == 2);
    }

    DrawVelModItem(d, kVelModTargets[idx][engine.velmod.target_idx[idx]], 112, 43, focus == 3);
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
    DrawFourSectionLayout(d, 67);
    const uint8_t focus = ui.velmod_focus[idx];

    {
        char vel_mon[4] = {};
        FormatVelocityMonitorString(vel_mon, diag);
        DrawVelModNumeric(d, vel_mon, 16, 16, false);
    }

    {
        constexpr int kCx = 48, kY = 48;
        char          buf[4] = {};
        std::snprintf(buf, sizeof(buf), "%u", engine.velmod.threshold[idx]);
        const int  w        = TinyStringWidth(buf);
        const int  x        = ClampInt(kCx - w / 2, 1, 127 - w);
        const bool focused  = (focus == 1);
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

    {
        constexpr int     kCx = 48;
        constexpr int     kY  = 48 + Font5x7::H + 2;
        static const char lbl[] = "link";
        const int         w = MiniString3x5Width(lbl);
        const int         x = ClampInt(kCx - w / 2, 1, 127 - w);
        if(engine.velmod.threshold_linked)
        {
            d.DrawRect(x - 1, kY - 1, x + w, kY + kMini3x5H, true, true);
            DrawMiniString3x5(d, lbl, x, kY, false);
        }
        else
        {
            DrawMiniString3x5(d, lbl, x, kY, true);
        }
    }

    {
        char buf[4] = {};
        std::snprintf(buf, sizeof(buf), "%u", engine.velmod.send_amount[idx]);
        DrawVelModNumeric(d, buf, 67, 18, focus == 2);
    }

    DrawVelModItem(d, kVelModTargets[idx][engine.velmod.target_idx[idx]], 112, 40, focus == 3);
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
    if(VelocityMod_HandleEvent(ui, engine, 0, e)) return true;
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
    if(VelocityMod_HandleEvent(ui, engine, 1, e)) return true;
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
    if(VelocityMod_HandleEvent(ui, engine, 0, e)) return true;
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
    if(VelocityMod_HandleEvent(ui, engine, 1, e)) return true;
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
        UiNav_Push(ui.ui_nav, UiScreenId::VelocityMod);
        ui.ui_dirty = true;
        return true;
    }
    return false;
}
