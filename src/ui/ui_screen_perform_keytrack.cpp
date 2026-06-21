#include "ui_screens_internal.h"

#include <cstdio>

#include "app_state_diagnostics.h"
#include "app_state_engine.h"
#include "app_state_ui.h"
#include "oled_pager.h"
#include "ui_draw_controls.h"
#include "ui_input.h"

// Keyboard background bitmap: 128 wide x 13 rows rendered, 16-byte stride.
// Copied verbatim from the keyzone screen so this screen can show the same
// keybed icon along the bottom edge.
static const uint8_t kPerformKeytrackKeyboard128x16[16 * 16] = {
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

// Focusable elements (L encoder cycles between them; each shows a solid border
// when focused and the R encoder edits the focused one).
static constexpr uint8_t kKtFocusVol    = 0u; // top-left label (placeholder)
static constexpr uint8_t kKtFocusAmount = 1u; // amount in dB (0..12)
static constexpr uint8_t kKtFocusMid    = 2u; // mid (pivot) note (C3..C6)
static constexpr uint8_t kKtFocusRect   = 3u; // tilt rectangle
static constexpr uint8_t kKtFocusCount  = 4u;

// Tilt rectangle bounds. The diagonal spans the full keybed width below it so it
// reads as one continuous volume curve across the whole keyboard (C1..C8); the
// keybed bitmap is drawn at x 0..127, so the line matches it edge to edge.
static constexpr int kKtRectX0 = 0;
static constexpr int kKtRectX1 = 127;
static constexpr int kKtRectY0 = 22; // box top = +amount dB (boost)
static constexpr int kKtRectY1 = 48; // box bottom = -amount dB (cut)

// MIDI note range the curve spans (C1..C8), matching the audio keytrack math.
static constexpr int kKtLoNote = 24;  // C1
static constexpr int kKtHiNote = 108; // C8

// Map a MIDI note to an x within the rectangle (C1 at left edge .. C8 at right).
static int KeytrackNoteToX(int note)
{
    if(note < kKtLoNote) note = kKtLoNote;
    if(note > kKtHiNote) note = kKtHiNote;
    const int span = kKtRectX1 - kKtRectX0;
    return kKtRectX0 + ((note - kKtLoNote) * span + (kKtHiNote - kKtLoNote) / 2)
                           / (kKtHiNote - kKtLoNote);
}

// Reset focus to the tilt rectangle whenever the screen becomes active.
void PerformKeytrack_OnScreenEnter(UiScreenCtx& ctx)
{
    if(ctx.ui)
        ctx.ui->perform_keytrack_focus = kKtFocusRect;
}

bool PerformKeytrack_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.engine)
        return false;
    if(ctx.lshift)
        return false;

    AppUiState&     ui     = *ctx.ui;
    AppEngineState& engine = *ctx.engine;

    // L encoder cycles focus: vol -> amount -> rectangle -> vol.
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        const int n = static_cast<int>(kKtFocusCount);
        int       f = static_cast<int>(ui.perform_keytrack_focus);
        f = (e.value > 0) ? (f + 1) % n : (f + n - 1) % n;
        ui.perform_keytrack_focus = static_cast<uint8_t>(f);
        ui.ui_dirty = true;
        return true;
    }

    // R encoder edits the focused element.
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const int delta = (e.value > 0) ? 1 : -1;
        if(ui.perform_keytrack_focus == kKtFocusRect)
        {
            const int maxv = static_cast<int>(AppEngineState::kPerformKeytrackTiltMax);
            int       t    = static_cast<int>(engine.keyzone.perform_keytrack_tilt) + delta;
            if(t < -maxv) t = -maxv;
            if(t > maxv)  t = maxv;
            engine.keyzone.perform_keytrack_tilt = static_cast<int8_t>(t);
            PublishEngineLayerParams(ctx);
            ui.ui_dirty = true;
            return true;
        }
        if(ui.perform_keytrack_focus == kKtFocusAmount)
        {
            const int maxv = static_cast<int>(AppEngineState::kPerformKeytrackAmountMaxDb);
            int       v    = static_cast<int>(engine.keyzone.perform_keytrack_amount_db) + delta;
            if(v < 0)    v = 0;
            if(v > maxv) v = maxv;
            engine.keyzone.perform_keytrack_amount_db = static_cast<int8_t>(v);
            PublishEngineLayerParams(ctx);
            ui.ui_dirty = true;
            return true;
        }
        if(ui.perform_keytrack_focus == kKtFocusMid)
        {
            const int lo = static_cast<int>(AppEngineState::kPerformKeytrackMidNoteMin);
            const int hi = static_cast<int>(AppEngineState::kPerformKeytrackMidNoteMax);
            int m = static_cast<int>(engine.keyzone.perform_keytrack_mid_note) + delta;
            if(m < lo) m = lo;
            if(m > hi) m = hi;
            engine.keyzone.perform_keytrack_mid_note = static_cast<uint8_t>(m);
            PublishEngineLayerParams(ctx);
            ui.ui_dirty = true;
            return true;
        }
        // vol is a placeholder for now — nothing to edit.
        return false;
    }

    return false;
}

void PerformKeytrack_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine || !ctx.display)
        return;

    AppUiState&     ui     = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    OledPager&      d      = *ctx.display;
    d.Fill(false);

    // Header label top-right, mirroring the keyzone screen's boxed label.
    static const char header_label[] = "keytrack";
    const int header_w = MicroStringWidth(header_label);
    const int box_w    = header_w + 4;
    const int box_h    = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, true);
    DrawMicroString(d, header_label, box_x + 2, 2, false);

    // Top-left focusable items: "vol" label (placeholder) and the amount in dB.
    // Each draws a solid 1 px border when focused.
    auto draw_focus_item = [&](const char* str, int x, int y, bool focused)
    {
        const int w = MicroStringWidth(str);
        DrawMicroString(d, str, x, y, true);
        if(focused)
            d.DrawRect(x - 2, y - 2, x + w + 1, y + kMicroH + 1, true, false);
        return w;
    };

    // Two label/value columns on the left (vol/amount over mid/pivot) plus a
    // read-only "mon" note monitor to the right.
    const int top_y  = 2;
    const int row2_y = 12;

    const int vol_w = draw_focus_item("vol", 2, top_y,
                                      ui.perform_keytrack_focus == kKtFocusVol);
    draw_focus_item("mid", 2, row2_y, false); // label only; value carries the focus

    const int val_x = 2 + vol_w + 6;

    // Amount (0..12 dB) — taller 5x7 tiny font so the "0" isn't clipped.
    char amt[8] = {};
    std::snprintf(amt, sizeof(amt), "%ddb",
                  static_cast<int>(engine.keyzone.perform_keytrack_amount_db));
    const int amt_w = TinyStringWidth(amt);
    DrawTinyString(d, amt, val_x, top_y, true);
    if(ui.perform_keytrack_focus == kKtFocusAmount)
        d.DrawRect(val_x - 2, top_y - 2, val_x + amt_w + 1, top_y + Font5x7::H + 1, true, false);

    // Mid (pivot) note name — focusable value under the amount.
    char mid_name[6] = {};
    FormatMidiNoteName(engine.keyzone.perform_keytrack_mid_note, mid_name, sizeof(mid_name));
    const int mid_w = TinyStringWidth(mid_name);
    DrawTinyStringCaseSensitive(d, mid_name, val_x, row2_y, true);
    if(ui.perform_keytrack_focus == kKtFocusMid)
        d.DrawRect(val_x - 2, row2_y - 2, val_x + mid_w + 1, row2_y + Font5x7::H + 1, true, false);

    // "mon" MIDI-note monitor (read-only): label on the top row, last played
    // note name below it. Fixed column (clears the widest value "12db") so it
    // doesn't shift when the amount changes digit count.
    const int mon_x = val_x + 30;
    DrawMicroString(d, "mon", mon_x, top_y, true);
    char mon_name[6] = {};
    uint32_t last_note = ctx.diag ? ctx.diag->last_note.load(std::memory_order_relaxed) : 0u;
    if(last_note > 127u)
        last_note = 127u;
    FormatMidiNoteName(static_cast<uint8_t>(last_note), mon_name, sizeof(mon_name));
    DrawTinyStringCaseSensitive(d, mon_name, mon_x, row2_y, true);

    // Keybed icon pinned to the bottom edge.
    DrawBitmap1bpp(d, 0, 64 - 13, 128, 13, 16, kPerformKeytrackKeyboard128x16, true);

    // Tilt graph: dotted 0 dB centreline + a two-segment tent that bends at the
    // mid note. Above the line = boost, below = cut; each side reaches the box
    // top/bottom (= +/-amount dB) at full tilt. tilt>0 cuts the high (right) side.
    const bool focused  = (ui.perform_keytrack_focus == kKtFocusRect);
    const int  tilt     = static_cast<int>(engine.keyzone.perform_keytrack_tilt);
    const int  maxv     = static_cast<int>(AppEngineState::kPerformKeytrackTiltMax);
    const int  center_y = (kKtRectY0 + kKtRectY1) / 2;
    const int  half_h   = (kKtRectY1 - kKtRectY0) / 2;
    const int  pivot_x  = KeytrackNoteToX(static_cast<int>(engine.keyzone.perform_keytrack_mid_note));

    const int mag    = (tilt < 0) ? -tilt : tilt;
    const int offset = (maxv > 0) ? (mag * half_h + maxv / 2) / maxv : 0;
    int left_y  = center_y;
    int right_y = center_y;
    if(tilt > 0) { left_y = center_y - offset; right_y = center_y + offset; }      // high side down
    else if(tilt < 0) { left_y = center_y + offset; right_y = center_y - offset; } // low side down

    // Dotted centreline (0 dB reference).
    for(int x = kKtRectX0; x <= kKtRectX1; x += 3)
        d.DrawPixel(x, center_y, true);
    // Short tick marking the pivot's x position.
    d.DrawLine(pivot_x, center_y - 2, pivot_x, center_y + 2, true);

    // Two-segment tent curve, bending at the pivot.
    d.DrawLine(kKtRectX0, left_y, pivot_x, center_y, true);
    d.DrawLine(pivot_x, center_y, kKtRectX1, right_y, true);

    // Solid box border only when focused.
    if(focused)
    {
        d.DrawRect(kKtRectX0, kKtRectY0, kKtRectX1, kKtRectY1, true, false);
    }
}
