// Host render of the perform-process screen. Links the REAL firmware draw code
// (ui_draw_text/shapes/controls.cpp) against a host OledPager and replays the
// exact draw calls from ui_screen_perform_process*.cpp, then dumps the 128x64
// framebuffer as rows of 0/1. Throwaway tool. Cursor = VOL (main_cursor 0).

#include "oled_pager.h"
#include "ui_draw_text.h"
#include "ui_draw_shapes.h"
#include "ui_draw_controls.h"

#include <cmath>
#include <cstdio>

static float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// ---- knob drawers, copied verbatim from ui_screen_perform_process_draw.cpp ----
static void KnobBody(OledPager& d, int cx, int cy, int r, float a)
{
    DrawCirclePixels(d, cx, cy, r, true);
    d.DrawPixel(cx, cy, true);
    const int hand_r = r - 2;
    const int hx = cx + (int)(std::cos(a) * (float)hand_r);
    const int hy = cy + (int)(std::sin(a) * (float)hand_r);
    d.DrawLine(cx, cy, hx, hy, true);
}
static void KnobLabel(OledPager& d, const char* s, int x, int y, bool focused)
{
    if(focused) DrawRencFocusMicroString(d, s, x, y);
    else        DrawMicroString(d, s, x, y, true);
}

int main()
{
    OledPager d;
    d.Fill(false);

    const int main_cursor   = 0;          // VOL focused
    const int selected_index = -1;        // not on a fader

    // ---- header box "process" (top-right), non-flash branch ----
    const char* header_label = "process";
    const int header_w = MicroStringWidth(header_label);
    const int box_w = header_w + 4;
    const int header_box_h = kMicroH + 4;
    int box_x = 128 - box_w; if(box_x < 0) box_x = 0;
    d.DrawRect(box_x, 0, box_x + box_w - 1, header_box_h - 1, true, true);
    DrawMicroString(d, header_label, box_x + 2, 2, false);

    const int box_y = 8;   // layout.y_body
    const int box_h = 56;  // y_footer(56) - y_body(8) + line_h(8) = 56

    // ---- left-pane triangle (DrawProcessLayerVolumePane) ----
    const int cx_mid = 0 + (60 / 2) - 1;   // 29
    const int vol_cy = box_y + 13;         // 21
    const int res_cy = box_y + box_h - 27; // 37
    const int cut_cy = box_y + box_h - 21; // 43
    const int res_cx = cx_mid - 15;        // 14
    const int cut_cx = cx_mid + 14;        // 43
    const int GAP = 4;                     // label-to-knob gap

    const float vol_angle = 2.0943951f + (0.5f * 5.2359878f); // default 0 dB -> norm 0.5
    KnobBody(d, cx_mid, vol_cy, 7, vol_angle);
    KnobLabel(d, "vol", cx_mid - (MicroStringWidth("vol") / 2), vol_cy - 7 - GAP - kMicroH, main_cursor == 0); // above

    const float res_angle = 2.0943951f + (0.0f * 5.2359878f);
    const float cut_angle = 2.0943951f + (1.0f * 5.2359878f);
    KnobBody(d, res_cx, res_cy, 6, res_angle);
    KnobLabel(d, "res", res_cx - (MicroStringWidth("res") / 2), res_cy + 6 + GAP, false);
    KnobBody(d, cut_cx, cut_cy, 6, cut_angle);
    KnobLabel(d, "cut", cut_cx - (MicroStringWidth("cut") / 2), cut_cy + 6 + GAP, false);

    // ---- right-pane FX faders (default fx_order {0,1,2,3}) ----
    const char* labels[4] = {"S", "", "D", "R"};
    float values[4] = {clamp01(0.5f), 1.0f, clamp01(0.35f), clamp01(0.6f)};
    bool hide_rails[4]   = {false, true, false, false};
    bool hide_handles[4] = {false, true, false, false};
    int label_y_offsets[4] = {-2, 0, -2, -2};
    int rail_bottom_clearance[4] = {3, 0, 3, 3};

    const int fader_x = 60, fader_w = 64, fader_y = box_y + 1, fader_h = box_h - 2;
    const int fader_left = fader_x + 4, fader_right = fader_x + fader_w - 5;
    const int span_x = fader_right - fader_left;
    int lane_x[4];
    for(int i = 0; i < 4; ++i)
        lane_x[i] = (i > 0 && span_x > 0) ? fader_left + (span_x * i) / 3 : fader_left;

    DrawVerticalFadersInRect(d, fader_x, fader_y, fader_w, fader_h, labels, values, 4,
                             false, -1, nullptr, nullptr, hide_rails, hide_handles,
                             1, 1, 1, 1, label_y_offsets, rail_bottom_clearance);

    // bit lane E/Q (fx_order index 1), not focused
    {
        const int i = 1;
        const int lane_left = (lane_x[i - 1] + lane_x[i]) / 2 + 1;
        const int lane_right = (lane_x[i] + lane_x[i + 1]) / 2 - 1;
        const int lane_top = fader_y + 1;
        const int lane_bottom = fader_y + fader_h - 2;
        const int top_w = TinyStringWidthCaseSensitiveTightColons("E");
        const int bottom_w = TinyStringWidthCaseSensitiveTightColons("Q");
        const int lane_cx = lane_left + ((lane_right - lane_left) / 2);
        const int gap = 4;
        const int total_h = (Font5x7::H * 2) + gap;
        const int top_y = lane_top + ((lane_bottom - lane_top - total_h) / 2);
        const int bottom_y = top_y + Font5x7::H + gap;
        DrawTinyStringCaseSensitive(d, "E", lane_cx - (top_w / 2), top_y, true);
        DrawTinyStringCaseSensitive(d, "Q", lane_cx - (bottom_w / 2), bottom_y, true);
    }

    // Emit lit pixels as run-length-encoded SVG rects (viewBox 0 0 128 64).
    for(int y = 0; y < 64; ++y)
    {
        int x = 0;
        while(x < 128)
        {
            if(!d.buf[y][x]) { ++x; continue; }
            int run = 0;
            while(x + run < 128 && d.buf[y][x + run]) ++run;
            std::printf("<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"1\"/>", x, y, run);
            x += run;
        }
    }
    std::putchar('\n');
    return 0;
}
