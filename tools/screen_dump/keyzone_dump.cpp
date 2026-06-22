// Throwaway host render of the keyzone mod blocks (rectangles, taller blocks,
// keyboard at bottom, left-edge labels, played-note flash marker). Replays the
// geometry from PerformKeyzone_Render against the host OledPager + real draws.
#include "oled_pager.h"
#include "ui_draw_text.h"
#include "ui_draw_shapes.h"
#include "ui_draw_controls.h"

#include <cstdio>

static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int keyzone_left_x(int midi_note)
{
    static constexpr int kLo = 36, kHi = 90;
    const int note = ClampInt(midi_note, kLo, kHi);
    const int span = kHi - kLo;
    return ((note - kLo) * 127 + (span / 2)) / span;
}

static const char* const kTargetTag[8] = {"", "vol", "att", "sus", "rel", "rev", "dly", "sat"};

static constexpr int kStatusH = 16, kKbdH = 13, kKbdY0 = 64 - kKbdH;
static constexpr int kBlockAY0 = kStatusH, kBlockAY1 = kBlockAY0 + 15;
static constexpr int kBlockBY0 = kBlockAY1 + 3, kBlockBY1 = kBlockBY0 + 15;

static void draw_lane_band(OledPager& d, int row_y0, int row_y1, int lane, bool focused,
                           int target, int source, int shape, int thr)
{
    if(target != 0)
    {
        const bool note_src = (source == 2) || (source == 3);
        const bool above    = (source == 2);
        int ax0 = 0, ax1 = 127;
        if(note_src) { const int tx = keyzone_left_x(thr); ax0 = above ? tx : 0; ax1 = above ? 127 : tx; }
        d.DrawRect(ax0, row_y0, ax1, row_y1, true, false);
        if(note_src && shape == 0 && ax1 > ax0)
        {
            if(above) d.DrawLine(ax0, row_y1, ax1, row_y0, true);
            else      d.DrawLine(ax0, row_y0, ax1, row_y1, true);
        }
    }
    else
        d.DrawRect(0, row_y0, 127, row_y1, true, false);

    const char* tag   = kTargetTag[target & 7];
    const char* label = (target != 0 && tag[0]) ? tag : (lane == 0 ? "a" : "b");
    const int lw = MicroStringWidth(label);
    const int tx = 3;
    const int ty = (row_y0 + row_y1) / 2 - (kMicroH / 2);
    if(focused) { DrawRencFocusFrame(d, tx, ty, lw, kMicroH); DrawMicroString(d, label, tx, ty, false); }
    else        { d.DrawRect(tx - 1, ty - 1, tx + lw, ty + kMicroH, false, true); DrawMicroString(d, label, tx, ty, true); }
}

static void dump(const char* title, int sA, int shA, int tgA, int thA, int sB, int shB, int tgB, int thB,
                 int focus, int flash_note, bool flash_on)
{
    OledPager d;
    d.Fill(false);
    draw_lane_band(d, kBlockAY0, kBlockAY1, 0, focus == 1, tgA, sA, shA, thA);
    draw_lane_band(d, kBlockBY0, kBlockBY1, 1, focus == 2, tgB, sB, shB, thB);
    // keyboard placeholder (real build blits the bitmap here)
    d.DrawRect(0, kKbdY0, 127, kKbdY0 + kKbdH - 1, true, false);
    if(flash_note >= 0 && flash_on)
    {
        const int nx = keyzone_left_x(flash_note);
        d.DrawRect(ClampInt(nx - 1, 0, 127), kKbdY0, ClampInt(nx + 1, 0, 127), kKbdY0 + kKbdH - 1, true, true);
    }
    std::printf("\n=== %s ===\n", title);
    for(int y = 0; y < 64; ++y)
    {
        std::printf("%2d ", y);
        for(int x = 0; x < 128; ++x) std::printf(d.buf[y][x] ? "#" : ".");
        std::printf("\n");
    }
}

int main()
{
    // src: 0=>vel 1=<vel 2=>note 3=<note ; shape: 0=knee 1=gate ; tgt: 5=rev 3=sus 1=vol
    dump("A: >note KNEE rev @C4 (focus) | B: <note KNEE sus @C4 | flash C5", 2, 0, 5, 60, 3, 0, 3, 60, 1, 72, true);
    dump("A: >note GATE rev @C4 | B: ---- empty (focus) | flash off",        2, 1, 5, 60, 0, 1, 0, 0,  2, -1, false);
    return 0;
}
