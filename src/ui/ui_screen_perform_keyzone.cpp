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
    if(!ctx.ui)
        return false;
    AppUiState& ui = *ctx.ui;
    // Single screen, three focusable slots: 0 = keytrack, 1 = lane A, 2 = lane B.
    if(ui.perform_keyzone_focus == 0u)
        return UiNav_Push(ui.ui_nav, UiScreenId::PerformKeytrack);
    if(ui.perform_keyzone_focus == 1u)
        return UiNav_Push(ui.ui_nav, UiScreenId::VelocityMod);
    if(ui.perform_keyzone_focus == 2u)
        return UiNav_Push(ui.ui_nav, UiScreenId::VelocityMod2);
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
    if(!ctx.ui)
        return false;
    if(ctx.lshift)
        return false;

    AppUiState& ui = *ctx.ui;

    // L encoder cycles focus: 0 = keytrack, 1 = lane A, 2 = lane B.
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        const uint8_t num_focus = 3u;
        if(e.value > 0)
            ui.perform_keyzone_focus = (ui.perform_keyzone_focus + 1u) % num_focus;
        else
            ui.perform_keyzone_focus = (ui.perform_keyzone_focus + num_focus - 1u) % num_focus;
        ui.ui_dirty = true;
        return true;
    }

    // External-encoder click enters the focused slot's subscreen.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(PerformKeyzone_TryPushSubscreen(ctx))
        {
            ui.ui_dirty = true;
            return true;
        }
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

    static const char header_label[] = "keyzone";
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
    // Keyboard icon sits at the very bottom; the two taller mod blocks fill the
    // space between the status bar and the keyboard.
    constexpr int kKbdH     = 13;
    constexpr int kKbdY0    = 64 - kKbdH;       // 51
    constexpr int kBlockAY0 = kStatusH;         // 16
    constexpr int kBlockAY1 = kBlockAY0 + 15;   // 31
    constexpr int kBlockBY0 = kBlockAY1 + 3;    // 34
    constexpr int kBlockBY1 = kBlockBY0 + 15;   // 49

    // Status-bar button label helper: left-anchored Font5x7 text.
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

    // Status-bar button (focus 0): opens the keytrack volume-tilt subscreen.
    const bool  kt_focused = (ui.perform_keyzone_focus == 0);
    const char* kt_str     = "keytrk";
    const int   kt_w       = TinyStringWidth(kt_str);
    const int   kt_tx      = 4;
    const int   kt_ty      = status_y;
    if(!kt_focused)
    {
        draw_mode_str(kt_str, kt_tx, kt_ty, true);
    }
    else
    {
        DrawFillOnlyTinyString(d, kt_tx, kt_ty, kt_w);
        draw_mode_str(kt_str, kt_tx, kt_ty, false);
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

    // 3-char tag per velmod target index (0 = "----" → blank).
    static const char* const kTargetTag[8]
        = {"", "vol", "att", "sus", "rel", "rev", "dly", "sat"};

    // Each lane (A/B) is a hollow rectangular mod block whose horizontal extent
    // shows its note coverage: >note spans the threshold rightward, <note from
    // the left up to the threshold, velocity sources span the whole width. A knee
    // shape adds a ramp diagonal (rising toward the domain's far end); a gate is
    // just the hollow box. Labelled on the left edge with the target (or just
    // a / b, lower-case, when no target is set).
    auto draw_lane_band = [&](int row_y0, int row_y1, int lane, bool focused)
    {
        const uint8_t target = engine.velmod.target_idx[lane];
        const uint8_t source = engine.velmod.source[lane];
        const uint8_t shape  = engine.velmod.shape[lane];
        const uint8_t thr    = engine.velmod.threshold[lane];

        if(target != 0u)
        {
            const bool note_src = (source == 2u) || (source == 3u); // >note / <note
            const bool above    = (source == 2u);                   // >note
            int        ax0      = 0;
            int        ax1      = 127;
            if(note_src)
            {
                const int thr_x = keyzone_left_x(thr);
                ax0 = above ? thr_x : 0;
                ax1 = above ? 127 : thr_x;
            }
            d.DrawRect(ax0, row_y0, ax1, row_y1, true, false); // hollow rectangle
            // Knee: a ramp diagonal from the 0 edge (at the threshold) up to the
            // full edge (the domain's far end). >note rises to the right, <note to
            // the left. Gate stays flat (just the hollow box).
            if(note_src && shape == 0u && ax1 > ax0)
            {
                if(above)
                    d.DrawLine(ax0, row_y1, ax1, row_y0, true); // ramp up to the right
                else
                    d.DrawLine(ax0, row_y0, ax1, row_y1, true); // ramp down to the right
            }
        }
        else
        {
            d.DrawRect(0, row_y0, 127, row_y1, true, false);  // empty lane outline
        }

        // Left-edge label: the target tag once a target is set, otherwise the
        // lane letter. Cleared background keeps it legible over the block fill;
        // the focused lane gets the inverted focus pill.
        const char* tag   = kTargetTag[target & 7u];
        const char* label = (target != 0u && tag[0] != '\0') ? tag
                                                             : (lane == 0 ? "a" : "b");
        const int lw = MicroStringWidth(label);
        const int tx = 3;
        const int ty = (row_y0 + row_y1) / 2 - (kMicroH / 2);
        if(focused)
        {
            DrawRencFocusFrame(d, tx, ty, lw, kMicroH);
            DrawMicroString(d, label, tx, ty, false);
        }
        else
        {
            d.DrawRect(tx - 1, ty - 1, tx + lw, ty + kMicroH, false, true);
            DrawMicroString(d, label, tx, ty, true);
        }
    };

    draw_lane_band(kBlockAY0, kBlockAY1, 0, ui.perform_keyzone_focus == 1);
    draw_lane_band(kBlockBY0, kBlockBY1, 1, ui.perform_keyzone_focus == 2);

    DrawBitmap1bpp(d, 0, kKbdY0, 128, kKbdH, 16, kPerformKeyzoneKeyboard128x16, true);

    // Played-note flash: a lit marker over the struck key, blinking ~3 Hz for a
    // short window after each note-on (window + note set by the UI tick in
    // ui_render.cpp). Only the on-phase draws, so an idle screen costs nothing.
    if(static_cast<int32_t>(ui.keyzone_flash_until_ms - ctx.now_ms) > 0
       && ((ctx.now_ms / 167u) & 1u) == 0u)
    {
        const int nx  = keyzone_left_x(ui.keyzone_flash_note);
        const int mx0 = ClampInt(nx - 1, 0, 127);
        const int mx1 = ClampInt(nx + 1, 0, 127);
        d.DrawRect(mx0, kKbdY0, mx1, kKbdY0 + kKbdH - 1, true, true);
    }

    // Bottom strip below the keyboard: lane A/B are the two bands, keytrack is
    // the status-bar slot, so nothing else is drawn here.
}
