#include "ui_screens_internal.h"

#include <cstdio>

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_shared.h"
#include "oled_pager.h"
#include "ui_draw_controls.h"
#include "ui_input.h"

// Keyboard background bitmap: 128 wide x 13 rows rendered, 16-byte stride.
// Copied verbatim from oled_ui_sim ui_ref/ui_screens.cpp.
static const uint8_t kPerformKeyzoneKeyboard128x16[16 * 16] = {
    0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C,
    0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C,
    0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C,
    0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C,
    0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C,
    0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
};

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo) return lo;
    if(v > hi) return hi;
    return v;
}

static void DrawFillOnlyTinyString(OledPager& d, int x, int y, int w)
{
    d.DrawRect(x - 1, y - 1, x + w, y + Font5x7::H, true, true);
}

static bool PerformKeyzone_TryPushSubscreen(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine)
        return false;
    AppUiState&     ui     = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    if(!engine.keyzone.perform_keyzone_is_split && ui.perform_keyzone_focus == 1u)
        return UiNav_Push(ui.ui_nav, UiScreenId::VelocityMod);
    if(engine.keyzone.perform_keyzone_is_split && ui.perform_keyzone_focus == 1u)
        return UiNav_Push(ui.ui_nav, UiScreenId::ModBlockA);
    if(engine.keyzone.perform_keyzone_is_split && ui.perform_keyzone_focus == 2u)
        return UiNav_Push(ui.ui_nav, UiScreenId::ModBlockB);
    return false;
}

// Definition is intentionally non-static: declared in ui_screens_internal.h so
// other screens (e.g. the Bake screen) can call it without duplicating the
// note-name table.
void FormatMidiNoteName(uint8_t note, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;
    static const char* kNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
    };
    const int midi_note = static_cast<int>(note);
    const int pitch     = ((midi_note % 12) + 12) % 12;
    const int octave    = (midi_note / 12) - 1;
    std::snprintf(out, out_n, "%s%d", kNames[pitch], octave);
}

// External-encoder click (on_enter slot): enter vel mod or mod block when focus matches.
bool PerformKeyzone_OnEnter(UiScreenCtx& ctx)
{
    if(PerformKeyzone_TryPushSubscreen(ctx))
    {
        ctx.ui->ui_dirty = true;
        return true;
    }
    return false;
}

bool PerformKeyzone_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.engine)
        return false;
    if(ctx.lshift)
        return false;

    AppUiState&     ui     = *ctx.ui;
    AppEngineState& engine = *ctx.engine;

    // L encoder cycles focus: CW = forward, CCW = backward.
    // FULL:  0=FULL/SPLIT  1=velocity Mod
    // SPLIT: 0=FULL/SPLIT  1=mod block A  2=mod block B
    // (Split point is moved by the R encoder anytime in SPLIT mode; it has no
    //  L-encoder focus slot.)
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        const bool    is_split  = engine.keyzone.perform_keyzone_is_split;
        const uint8_t num_focus = is_split ? 3u : 2u;
        // SPLIT scroll direction is reversed: a CW turn from SPLIT (focus 0)
        // lands on mod block B (focus 2) first, then mod block A, then SPLIT.
        bool forward = (e.value > 0);
        if(is_split)
            forward = !forward;
        if(forward)
            ui.perform_keyzone_focus = (ui.perform_keyzone_focus + 1u) % num_focus;
        else
            ui.perform_keyzone_focus = (ui.perform_keyzone_focus + num_focus - 1u) % num_focus;
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(PerformKeyzone_TryPushSubscreen(ctx))
        {
            ui.ui_dirty = true;
            return true;
        }
    }

    // R encoder click on FULL/SPLIT button (focus==0): toggle split mode.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc
       && ui.perform_keyzone_focus == 0)
    {
        engine.keyzone.perform_keyzone_is_split = !engine.keyzone.perform_keyzone_is_split;
        if(engine.keyzone.perform_keyzone_is_split)
        {
            engine.keyzone.perform_keyzone_lo_note[0] = 12u;  // C0
            engine.keyzone.perform_keyzone_hi_note[0] = 59u;  // B3
            engine.keyzone.perform_keyzone_lo_note[1] = 60u;  // C4
            engine.keyzone.perform_keyzone_hi_note[1] = 108u; // C8
        }
        else
        {
            engine.keyzone.perform_keyzone_lo_note[0] = 12u;
            engine.keyzone.perform_keyzone_hi_note[0] = 108u;
            engine.keyzone.perform_keyzone_lo_note[1] = 12u;
            engine.keyzone.perform_keyzone_hi_note[1] = 108u;
            if(ui.perform_keyzone_focus > 1u)
                ui.perform_keyzone_focus = 0u;
        }
        PublishEngineLayerParams(ctx);
        ui.ui_dirty = true;
        return true;
    }

    // R encoder rotation in SPLIT mode: move the split point.
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0
       && engine.keyzone.perform_keyzone_is_split)
    {
        const int delta    = (e.value > 0) ? 1 : -1;
        const int a_hi     = static_cast<int>(engine.keyzone.perform_keyzone_hi_note[0]);
        const int new_a_hi = ClampInt(a_hi + delta,
                                      static_cast<int>(engine.keyzone.perform_keyzone_lo_note[0]),
                                      static_cast<int>(engine.keyzone.perform_keyzone_hi_note[1]) - 1);
        if(new_a_hi == a_hi)
            return false;
        engine.keyzone.perform_keyzone_hi_note[0] = static_cast<uint8_t>(new_a_hi);
        engine.keyzone.perform_keyzone_lo_note[1] = static_cast<uint8_t>(new_a_hi + 1);
        PublishEngineLayerParams(ctx);
        ui.ui_dirty = true;
        return true;
    }

    return false;
}

void PerformKeyzone_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    AppUiState&     ui     = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    EngineRefreshLoadedMetadata(*ctx.ui, *ctx.engine, *ctx.shared);

    OledPager& d = *ctx.display;
    d.Fill(false);

    static const char header_label[] = "kyzn a+b";
    const int header_w = MicroStringWidth(header_label);
    const int box_w    = header_w + 4;
    const int box_h    = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash
        = static_cast<int32_t>(engine.layer.engine_header_invert_until_ms - ctx.now_ms) > 0;
    if(header_invert_flash)
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, false, true);
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true,  false);
        DrawMicroString(d, header_label, box_x + 2, 2, true);
    }
    else
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, true);
        DrawMicroString(d, header_label, box_x + 2, 2, false);
    }

    constexpr int kStatusH = 16;
    constexpr int kBarH    = 9;

    // Mode button (FULL/SPLIT): left-anchored.
    // Uses Font5x7 directly to avoid the 9px tall-H/L special case in DrawTinyString.
    auto draw_mode_str = [&](const char* str, int x, int y, bool on)
    {
        for(int i = 0; str[i] != '\0'; ++i)
        {
            uint8_t rows[Font5x7::H] = {};
            Font5x7::GetGlyphRows(str[i], rows);
            for(int yy = 0; yy < Font5x7::H; ++yy)
            {
                const uint8_t row = rows[yy];
                for(int xx = 0; xx < Font5x7::W; ++xx)
                {
                    if((row >> (Font5x7::W - 1 - xx)) & 1)
                    {
                        const int px = x + i * (Font5x7::W + 1) + xx;
                        const int py = y + yy;
                        if(px >= 0 && px < 128 && py >= 0 && py < 64)
                            d.DrawPixel(px, py, on);
                    }
                }
            }
        }
    };

    const int status_y = (kStatusH - Font5x7::H) / 2;

    const bool  is_split     = engine.keyzone.perform_keyzone_is_split;
    const bool  mode_focused = (ui.perform_keyzone_focus == 0);
    const char* mode_str     = is_split ? "SPLIT" : "FULL";
    const int   mode_w       = TinyStringWidth(mode_str);
    const int   mode_pad     = 2;
    const int   mode_bx0     = 2;
    const int   mode_tx      = mode_bx0 + mode_pad;
    const int   mode_ty      = status_y;
    if(!mode_focused)
    {
        draw_mode_str(mode_str, mode_tx, mode_ty, true);
    }
    else
    {
        if(is_split)
            DrawRencFocusFrame(d, mode_tx, mode_ty, mode_w, Font5x7::H);
        else
            DrawFillOnlyTinyString(d, mode_tx, mode_ty, mode_w);
        draw_mode_str(mode_str, mode_tx, mode_ty, false);
    }

    // Split-point label "X:X" centered in status bar, SPLIT mode only.
    if(is_split)
    {
        char split_lo_note[8] = {};
        char split_hi_note[8] = {};
        char split_text[20]   = {};
        FormatMidiNoteName(engine.keyzone.perform_keyzone_hi_note[0], split_lo_note, sizeof(split_lo_note));
        FormatMidiNoteName(engine.keyzone.perform_keyzone_lo_note[1], split_hi_note, sizeof(split_hi_note));
        std::snprintf(split_text, sizeof(split_text), "%s:%s", split_lo_note, split_hi_note);

        const int split_w        = TinyStringWidthCaseSensitiveTightColons(split_text);
        const int split_x        = (128 - split_w) / 2;
        DrawTinyStringCaseSensitive(d, split_text, split_x, status_y, true);
    }

    auto keyzone_left_x = [&](uint8_t midi_note)
    {
        static constexpr int kKeyzoneLoMidi = 36; // C2
        static constexpr int kKeyzoneHiMidi = 90; // F#6
        const int note   = ClampInt(static_cast<int>(midi_note), kKeyzoneLoMidi, kKeyzoneHiMidi);
        const int span   = kKeyzoneHiMidi - kKeyzoneLoMidi;
        const int offset = note - kKeyzoneLoMidi;
        return (offset * 127 + (span / 2)) / span;
    };

    auto draw_layer_box = [&](int rect_x0, int rect_x1, int section_index,
                               const char* label, bool show_arrows = true)
    {
        const int section_y0 = kStatusH + (section_index - 1) * kBarH;
        const int rect_y0    = section_y0;
        const int rect_y1    = section_y0 + kBarH - 1;
        d.DrawRect(rect_x0, rect_y0, rect_x1, rect_y1, true, false);

        const int label_y  = rect_y0 + ((kBarH - kMicroH) / 2);
        const int center_y = rect_y0 + kBarH / 2;
        const int lw       = MicroStringWidth(label);
        const int bar_w    = rect_x1 - rect_x0 + 1;
        const int text_x   = rect_x0 + (bar_w - lw) / 2;

        DrawMicroString(d, label, text_x, label_y, true);

        if(!show_arrows)
            return;

        // Stretch arrows: filled triangular arrowhead at each bar edge.
        constexpr int kArrowW = 3;
        constexpr int kGap    = 2;

        // Left arrow (←)
        {
            const int x_tip    = rect_x0 + 1;
            const int shaft_x0 = x_tip + kArrowW;
            const int shaft_x1 = text_x - kGap - 1;
            for(int dy = -2; dy <= 2; ++dy)
            {
                const int py     = center_y + dy;
                const int abs_dy = dy < 0 ? -dy : dy;
                const int ax0 = x_tip + abs_dy;
                const int ax1 = x_tip + kArrowW - 1;
                if(ax1 >= ax0 && py > rect_y0 && py < rect_y1)
                    d.DrawLine(ax0, py, ax1, py, true);
            }
            if(shaft_x1 >= shaft_x0)
                d.DrawLine(shaft_x0, center_y, shaft_x1, center_y, true);
        }

        // Right arrow (→)
        {
            const int x_tip    = rect_x1 - 1;
            const int shaft_x0 = text_x + lw + kGap;
            const int shaft_x1 = x_tip - kArrowW;
            for(int dy = -2; dy <= 2; ++dy)
            {
                const int py     = center_y + dy;
                const int abs_dy = dy < 0 ? -dy : dy;
                const int ax0 = x_tip - (kArrowW - 1);
                const int ax1 = x_tip - abs_dy;
                if(ax1 >= ax0 && py > rect_y0 && py < rect_y1)
                    d.DrawLine(ax0, py, ax1, py, true);
            }
            if(shaft_x1 >= shaft_x0)
                d.DrawLine(shaft_x0, center_y, shaft_x1, center_y, true);
        }
    };

    const int a_rect_x0 = keyzone_left_x(engine.keyzone.perform_keyzone_lo_note[0]);
    const int a_rect_x1 = ClampInt(keyzone_left_x(engine.keyzone.perform_keyzone_hi_note[0]), a_rect_x0, 127);
    const int b_rect_x0 = keyzone_left_x(engine.keyzone.perform_keyzone_lo_note[1]);
    const int b_rect_x1 = ClampInt(keyzone_left_x(engine.keyzone.perform_keyzone_hi_note[1]), b_rect_x0, 127);

    draw_layer_box(a_rect_x0, a_rect_x1, 1, is_split ? "a" : "layer a", !is_split);
    draw_layer_box(b_rect_x0, b_rect_x1, 2, is_split ? "b" : "layer b", !is_split);

    // Split-connection dots when A hi+1 == B lo (or vice versa).
    const int a_dot_y = kStatusH + 0 * kBarH + kBarH / 2;
    const int b_dot_y = kStatusH + 1 * kBarH + kBarH / 2;
    if(static_cast<int>(engine.keyzone.perform_keyzone_hi_note[0]) + 1
       == static_cast<int>(engine.keyzone.perform_keyzone_lo_note[1]))
    {
        if(a_rect_x1 + 3 < 128 && a_dot_y + 1 < 64)
            d.DrawRect(a_rect_x1 + 2, a_dot_y, a_rect_x1 + 3, a_dot_y + 1, true, true);
        if(b_rect_x0 - 3 >= 0 && b_dot_y + 1 < 64)
            d.DrawRect(b_rect_x0 - 3, b_dot_y, b_rect_x0 - 2, b_dot_y + 1, true, true);
    }
    if(static_cast<int>(engine.keyzone.perform_keyzone_hi_note[1]) + 1
       == static_cast<int>(engine.keyzone.perform_keyzone_lo_note[0]))
    {
        if(b_rect_x1 + 3 < 128 && b_dot_y + 1 < 64)
            d.DrawRect(b_rect_x1 + 2, b_dot_y, b_rect_x1 + 3, b_dot_y + 1, true, true);
        if(a_rect_x0 - 3 >= 0 && a_dot_y + 1 < 64)
            d.DrawRect(a_rect_x0 - 3, a_dot_y, a_rect_x0 - 2, a_dot_y + 1, true, true);
    }

    DrawBitmap1bpp(d, 0, kStatusH + 2 * kBarH, 128, 13, 16, kPerformKeyzoneKeyboard128x16, true);

    const int bottom_y0 = kStatusH + 2 * kBarH + 13;

    if(is_split)
    {
        // Two mod-block rectangles aligned to the layer A/B split positions.
        static const char kModBlocksLabel[] = "mod blocks";
        const int mod_rect_h  = 7;
        const int mod_gap     = 2;
        const int content_h   = mod_rect_h + mod_gap + kMicroH;
        const int mod_margin  = (64 - bottom_y0 - content_h) / 2;
        const int mod_rect_y0 = bottom_y0 + mod_margin;
        const int mod_rect_y1 = mod_rect_y0 + mod_rect_h - 1;
        const int mod_text_y  = mod_rect_y1 + 1 + mod_gap;

        const bool a_focused = (ui.perform_keyzone_focus == 1);
        const bool b_focused = (ui.perform_keyzone_focus == 2);

        if(a_focused)
        {
            d.DrawRect(0, mod_rect_y0 - 2,
                       ClampInt(a_rect_x1 + 2, 0, 127), mod_rect_y1 + 2, true, false);
            d.DrawRect(0, mod_rect_y0, a_rect_x1, mod_rect_y1, true, true);
        }
        else
        {
            d.DrawRect(0, mod_rect_y0, a_rect_x1, mod_rect_y1, true, false);
        }

        if(b_focused)
        {
            d.DrawRect(ClampInt(b_rect_x0 - 2, 0, 127), mod_rect_y0 - 2,
                       127, mod_rect_y1 + 2, true, false);
            d.DrawRect(b_rect_x0, mod_rect_y0, 127, mod_rect_y1, true, true);
        }
        else
        {
            d.DrawRect(b_rect_x0, mod_rect_y0, 127, mod_rect_y1, true, false);
        }

        const int mod_lbl_w = MicroStringWidth(kModBlocksLabel);
        DrawMicroString(d, kModBlocksLabel, (128 - mod_lbl_w) / 2, mod_text_y, true);
    }
    else
    {
        // "velocity Mod" button centered in the space below the keyboard.
        static const char kVelModLabel[] = "velocity Mod";
        const int vel_w  = TinyStringWidth(kVelModLabel);
        const int vel_tx = (128 - vel_w) / 2;
        const int vel_ty = bottom_y0 + (64 - bottom_y0 - Font5x7::H) / 2;
        if(ui.perform_keyzone_focus == 1)
        {
            DrawFillOnlyTinyString(d, vel_tx, vel_ty, vel_w);
            draw_mode_str(kVelModLabel, vel_tx, vel_ty, false);
        }
        else
        {
            draw_mode_str(kVelModLabel, vel_tx, vel_ty, true);
        }
    }
}
