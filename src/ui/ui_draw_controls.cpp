#include "ui_draw_controls.h"

#include "ui_draw_text.h"
#include "ui_draw_shapes.h"
#include "oled_pager.h"

#include <cstring>

using namespace daisy;

static int ClampCoord(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

void DrawRencFocusFrame(OledPager& d, int x, int y, int w, int h)
{
    if(w <= 0 || h <= 0)
        return;

    const int ox0 = ClampCoord(x - 3, 0, 127);
    const int oy0 = ClampCoord(y - 3, 0, 63);
    const int ox1 = ClampCoord(x + w + 2, 0, 127);
    const int oy1 = ClampCoord(y + h + 2, 0, 63);
    const int ix0 = ClampCoord(x - 1, 0, 127);
    const int iy0 = ClampCoord(y - 1, 0, 63);
    const int ix1 = ClampCoord(x + w, 0, 127);
    const int iy1 = ClampCoord(y + h, 0, 63);

    d.DrawRect(ox0, oy0, ox1, oy1, false, true);
    d.DrawRect(ox0, oy0, ox1, oy1, true, false);
    d.DrawRect(ix0, iy0, ix1, iy1, true, true);
}

void DrawRencFocusTinyString(OledPager& d, const char* str, int x, int y)
{
    if(!str)
        return;
    DrawRencFocusFrame(d, x, y, TinyStringWidth(str), Font5x7::H);
    DrawTinyString(d, str, x, y, false);
}

void DrawRencFocusMicroString(OledPager& d, const char* str, int x, int y)
{
    if(!str)
        return;
    DrawRencFocusFrame(d, x, y, MicroStringWidth(str), kMicroH);
    DrawMicroString(d, str, x, y, false);
}

void DrawRencFocusString6x8(OledPager& d, const char* str, int x, int y)
{
    if(!str)
        return;
    const int w = static_cast<int>(std::strlen(str)) * 6;
    DrawRencFocusFrame(d, x, y, w, 8);
    d.SetCursor(x, y);
    d.WriteString(str, Font_6x8, false);
}

void DrawVerticalFadersInRect(OledPager& d,
                              int x,
                              int y,
                              int w,
                              int h,
                              const char* const* labels,
                              const float* values,
                              int count,
                              bool select_active,
                              int selected_index,
                              const int* x_offsets,
                              const bool* circle_handles,
                              const bool* hide_rails,
                              const bool* hide_handles,
                              int selected_label_box_y_offset,
                              int selected_label_box_extra_bottom,
                              int selected_label_box_bottom_clip_extra,
                              int selected_label_style,
                              const int* label_y_offsets,
                              const int* rail_bottom_clearance)
{
    if(w <= 2 || h <= 2 || count <= 0)
        return;

    // Keep edge lanes far enough from bounds so 7px handles stay centered on rails.
    int fader_left = x + 4;
    int fader_right = x + w - 5;
    if(fader_right <= fader_left)
        return;

    const int span_x = fader_right - fader_left;
    for(int f = 0; f < count; ++f)
    {
        const int label_y = y + h - Font5x7::H - 1
                            + ((label_y_offsets != nullptr) ? label_y_offsets[f] : 0);
        const int line_top = y + 2;
        const int line_bottom = label_y - 2
                                - ((rail_bottom_clearance != nullptr) ? rail_bottom_clearance[f] : 0);
        if(line_bottom <= line_top)
            continue;
        const int span_y = line_bottom - line_top;

        int line_x = fader_left;
        if(count > 1 && span_x > 0)
            line_x = fader_left + (span_x * f) / (count - 1);
        if(x_offsets != nullptr)
            line_x += x_offsets[f];

        const char* label = labels[f];
        const int label_w = TinyStringWidth(label);
        int label_x = line_x - (label_w / 2);
        if(label_x < x + 1)
            label_x = x + 1;
        if(label_x + label_w > x + w - 2)
            label_x = x + w - 2 - label_w;
        const bool selected = select_active && f == selected_index;
        const bool line_on = true;
        int rail_x = line_x;
        if(rail_x < x + 1)
            rail_x = x + 1;
        if(rail_x > x + w - 2)
            rail_x = x + w - 2;

        if(hide_rails == nullptr || !hide_rails[f])
        {
            d.DrawLine(rail_x, line_top, rail_x, line_bottom, line_on);
            d.DrawLine(rail_x - 1, line_top, rail_x + 1, line_top, line_on);
            d.DrawLine(rail_x - 1, line_bottom, rail_x + 1, line_bottom, line_on);
        }

        const float value = values[f];
        int tick_y = line_bottom - static_cast<int>(value * static_cast<float>(span_y) + 0.5f);
        const bool hide_handle = (hide_handles != nullptr && hide_handles[f]);
        const bool draw_circle = (circle_handles != nullptr && circle_handles[f]);
        if(hide_handle)
        {
            // no handle
        }
        else if(draw_circle)
        {
            const int r = 2;
            int cx = line_x;
            int cy = tick_y;
            if(cx - r < x + 1) cx = x + 1 + r;
            if(cx + r > x + w - 2) cx = x + w - 2 - r;
            if(cy - r < line_top) cy = line_top + r;
            if(cy + r > line_bottom) cy = line_bottom - r;
            d.DrawRect(cx - r, cy - r, cx + r, cy + r, true, false);
            d.DrawRect(cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1, false, true);
            d.DrawPixel(cx - r, cy - r, false);
            d.DrawPixel(cx + r, cy - r, false);
            d.DrawPixel(cx - r, cy + r, false);
            d.DrawPixel(cx + r, cy + r, false);
        }
        else
        {
            int handle_w = 7; // odd width keeps visual center on rail
            const int max_w = (x + w - 2) - (x + 1) + 1;
            if(handle_w > max_w)
                handle_w = max_w;
            if((handle_w & 1) == 0 && handle_w > 1)
                --handle_w;
            int handle_x0 = line_x - handle_w / 2;
            int handle_x1 = handle_x0 + handle_w - 1;
            if(handle_x0 < x + 1)
            {
                handle_x0 = x + 1;
                handle_x1 = handle_x0 + handle_w - 1;
            }
            if(handle_x1 > x + w - 2)
            {
                handle_x1 = x + w - 2;
                handle_x0 = handle_x1 - handle_w + 1;
            }
            int handle_y0 = tick_y - 5;
            int handle_y1 = tick_y + 5;
            if(handle_y0 < line_top)
                handle_y0 = line_top;
            if(handle_y1 > line_bottom)
                handle_y1 = line_bottom;

            d.DrawRect(handle_x0, handle_y0, handle_x1, handle_y1, true, false);
            if(handle_x1 - handle_x0 >= 2 && handle_y1 - handle_y0 >= 2)
                d.DrawRect(handle_x0 + 1, handle_y0 + 1, handle_x1 - 1, handle_y1 - 1, false, true);
            if((handle_x1 - handle_x0) >= 4 && (handle_y1 - handle_y0) >= 4)
            {
                d.DrawPixel(handle_x0, handle_y0, false);
                d.DrawPixel(handle_x1, handle_y0, false);
                d.DrawPixel(handle_x0, handle_y1, false);
                d.DrawPixel(handle_x1, handle_y1, false);
            }
            const int center_y = handle_y0 + ((handle_y1 - handle_y0) / 2);
            const int inner_x0 = handle_x0 + 1;
            const int inner_x1 = handle_x1 - 1;
            if(inner_x1 > inner_x0)
            {
                d.DrawLine(inner_x0, center_y, inner_x1, center_y, true);
                if(center_y - 2 >= handle_y0 + 1)
                    d.DrawLine(inner_x0, center_y - 2, inner_x1, center_y - 2, true);
                if(center_y + 2 <= handle_y1 - 1)
                    d.DrawLine(inner_x0, center_y + 2, inner_x1, center_y + 2, true);
            }
        }

        if(label_x + label_w < x + w - 1)
        {
            if(selected)
            {
                if(selected_label_style == 1)
                {
                    const int x0 = label_x - 2;
                    const int y0 = label_y - 2;
                    const int x1 = label_x + label_w + 1;
                    const int y1 = label_y + Font5x7::H + 1;
                    d.DrawRect(x0, y0, x1, y1, true, true);
                    DrawDottedRect(d, x0 - 2, y0 - 2, x1 + 2, y1 + 2, true);
                    DrawTinyString(d, label, label_x, label_y, false);
                }
                else if(selected_label_style == 2)
                {
                    d.DrawRect(label_x - 2, label_y - 2, label_x + label_w + 1, label_y + Font5x7::H + 1, true, true);
                    DrawTinyString(d, label, label_x, label_y, false);
                }
                else if(selected_label_style == 3)
                {
                    d.DrawRect(label_x - 2, label_y - 2, label_x + label_w + 1, label_y + Font5x7::H + 1, true, false);
                    DrawTinyString(d, label, label_x, label_y, true);
                }
                else if(selected_label_style == 4)
                {
                    DrawDottedRect(d, label_x - 2, label_y - 2, label_x + label_w + 1, label_y + Font5x7::H + 1, true);
                    DrawTinyString(d, label, label_x, label_y, true);
                }
                else
                {
                    DrawRencFocusTinyString(d, label, label_x, label_y);
                }
            }
            else
            {
                DrawTinyString(d, label, label_x, label_y, true);
            }
        }
    }
}

void DrawDelayDetailFaders(OledPager& d,
                           int x,
                           int y,
                           int w,
                           int h,
                           const char* const labels[4],
                           const float values[4],
                           bool select_active,
                           int selected_index,
                           int out_lbl_x0[4],
                           int out_lbl_y0[4],
                           int out_lbl_x1[4],
                           int out_lbl_y1[4])
{
    for(int i = 0; i < 4; ++i)
    {
        out_lbl_x0[i] = 0;
        out_lbl_y0[i] = 0;
        out_lbl_x1[i] = 0;
        out_lbl_y1[i] = 0;
    }
    if(w <= 2 || h <= 2)
        return;

    constexpr int kShiftLtmRtmFbkPx = 5;
    constexpr int kMixLabelLeftPx   = 3;
    const int       line_top    = y + 2;
    const int       line_bottom = y + h - 2;
    if(line_bottom <= line_top)
        return;

    int fader_left = x + 4;
    int fader_right = x + w - 5;
    if(fader_right <= fader_left)
        return;

    const int span_x = fader_right - fader_left;
    constexpr int kCount = 4;

    for(int f = 0; f < kCount; ++f)
    {
        int line_x = fader_left;
        if(kCount > 1 && span_x > 0)
            line_x = fader_left + (span_x * f) / (kCount - 1);
        // LTM/RTM/FBK: rails shift right; labels stay on the unshifted column grid.
        const int rail_x_center = line_x + ((f <= 2) ? kShiftLtmRtmFbkPx : 0);

        const char* lab = labels[f];
        int         lab_w = 0;
        int         lab_x = 0;
        int         lab_y = y + h - Font5x7::H - 1;
        if(lab != nullptr && lab[0] != '\0')
        {
            lab_w = TinyStringWidth(lab);
            lab_x = rail_x_center - (lab_w / 2);
            if(f == 3)
                lab_x -= kMixLabelLeftPx;
            if(lab_x < x + 1)
                lab_x = x + 1;
            if(lab_x + lab_w > x + w - 2)
                lab_x = x + w - 2 - lab_w;
            out_lbl_x0[f] = lab_x - 1;
            out_lbl_y0[f] = lab_y - 1;
            out_lbl_x1[f] = lab_x + lab_w;
            out_lbl_y1[f] = lab_y + Font5x7::H;
        }

        int rail_x = rail_x_center;
        if(rail_x < x + 1)
            rail_x = x + 1;
        if(rail_x > x + w - 2)
            rail_x = x + w - 2;

        const int rail_top    = line_top;
        const int rail_bottom = line_bottom;
        const int span_y_rail = rail_bottom - rail_top;
        if(span_y_rail < 1)
            continue;

        d.DrawLine(rail_x, rail_top, rail_x, rail_bottom, true);
        d.DrawLine(rail_x - 1, rail_top, rail_x + 1, rail_top, true);
        d.DrawLine(rail_x - 1, rail_bottom, rail_x + 1, rail_bottom, true);

        const float value  = values[f];
        int         tick_y = rail_bottom - static_cast<int>(value * static_cast<float>(span_y_rail) + 0.5f);

        int handle_w = 7;
        const int max_w = (x + w - 2) - (x + 1) + 1;
        if(handle_w > max_w)
            handle_w = max_w;
        if((handle_w & 1) == 0 && handle_w > 1)
            --handle_w;
        int handle_x0 = rail_x - handle_w / 2;
        int handle_x1 = handle_x0 + handle_w - 1;
        if(handle_x0 < x + 1)
        {
            handle_x0 = x + 1;
            handle_x1 = handle_x0 + handle_w - 1;
        }
        if(handle_x1 > x + w - 2)
        {
            handle_x1 = x + w - 2;
            handle_x0 = handle_x1 - handle_w + 1;
        }
        int handle_y0 = tick_y - 5;
        int handle_y1 = tick_y + 5;
        if(handle_y0 < rail_top)
            handle_y0 = rail_top;
        if(handle_y1 > rail_bottom)
            handle_y1 = rail_bottom;

        d.DrawRect(handle_x0, handle_y0, handle_x1, handle_y1, true, false);
        if(handle_x1 - handle_x0 >= 2 && handle_y1 - handle_y0 >= 2)
            d.DrawRect(handle_x0 + 1, handle_y0 + 1, handle_x1 - 1, handle_y1 - 1, false, true);
        if((handle_x1 - handle_x0) >= 4 && (handle_y1 - handle_y0) >= 4)
        {
            d.DrawPixel(handle_x0, handle_y0, false);
            d.DrawPixel(handle_x1, handle_y0, false);
            d.DrawPixel(handle_x0, handle_y1, false);
            d.DrawPixel(handle_x1, handle_y1, false);
        }
        const int center_y = handle_y0 + ((handle_y1 - handle_y0) / 2);
        const int inner_x0 = handle_x0 + 1;
        const int inner_x1 = handle_x1 - 1;
        if(inner_x1 > inner_x0)
        {
            d.DrawLine(inner_x0, center_y, inner_x1, center_y, true);
            if(center_y - 2 >= handle_y0 + 1)
                d.DrawLine(inner_x0, center_y - 2, inner_x1, center_y - 2, true);
            if(center_y + 2 <= handle_y1 - 1)
                d.DrawLine(inner_x0, center_y + 2, inner_x1, center_y + 2, true);
        }

        // Draw labels after rails/handles so controls visually pass behind titles.
        if(lab != nullptr && lab[0] != '\0')
        {
            // Mask only the text cell (not full label box) so low handles can still peek above label.
            d.DrawRect(lab_x, lab_y, lab_x + lab_w - 1, lab_y + Font5x7::H - 1, false, true);
            const bool selected = select_active && f == selected_index;
            if(selected && f >= 2)
                d.DrawRect(out_lbl_x0[f], out_lbl_y0[f], out_lbl_x1[f], out_lbl_y1[f], true, false);
            DrawTinyString(d, lab, lab_x, lab_y, true);
        }
    }
}
