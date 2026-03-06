#include "ui_screens.h"

#include "app_state.h"
#include "params.h"
#include "ui_input.h"
#include "ui_list_menu.h"
#include "ui_value_edit.h"
#include "ui_layout.h"
#include "oled_pager.h"
#include "mod_matrix.h"
#include "macros.h"
#include "ui_requests.h"
#include "sd_browser_state.h"
#include "sample_edit.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace daisy;

static float Clamp01(float x)
{
    if(x < 0.0f) return 0.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

static float ClampSigned(float x)
{
    if(x < -1.0f) return -1.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

static int ToPct01(float x)
{
    if(x < 0.0f) x = 0.0f;
    if(x > 1.0f) x = 1.0f;
    return static_cast<int>(x * 100.0f + 0.5f);
}

static char DstChar(uint8_t dst)
{
    return (dst == static_cast<uint8_t>(ModDest::FilterCutoff)) ? 'C' : 'P';
}

struct Font5x7
{
    static constexpr int W = 5;
    static constexpr int H = 7;

    static void GetGlyphRows(char c, uint8_t out_rows[H])
    {
        if(c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');

        auto set = [&](std::initializer_list<uint8_t> rows)
        {
            int i = 0;
            for(auto r : rows)
                out_rows[i++] = r;
        };

        if(c == ' ')
        {
            set({0, 0, 0, 0, 0, 0, 0});
            return;
        }

        switch(c)
        {
            case '0': set({0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}); return;
            case '1': set({0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}); return;
            case '2': set({0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}); return;
            case '3': set({0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}); return;
            case '4': set({0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}); return;
            case '5': set({0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}); return;
            case '6': set({0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}); return;
            case '7': set({0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}); return;
            case '8': set({0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}); return;
            case '9': set({0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}); return;
            default: break;
        }

        switch(c)
        {
            case 'A': set({0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}); return;
            case 'B': set({0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}); return;
            case 'C': set({0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}); return;
            case 'D': set({0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110}); return;
            case 'E': set({0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}); return;
            case 'F': set({0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}); return;
            case 'G': set({0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110}); return;
            case 'H': set({0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}); return;
            case 'I': set({0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}); return;
            case 'J': set({0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100}); return;
            case 'K': set({0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}); return;
            case 'L': set({0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}); return;
            case 'M': set({0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}); return;
            case 'N': set({0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001}); return;
            case 'O': set({0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}); return;
            case 'P': set({0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}); return;
            case 'Q': set({0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}); return;
            case 'R': set({0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}); return;
            case 'S': set({0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}); return;
            case 'T': set({0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}); return;
            case 'U': set({0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}); return;
            case 'V': set({0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}); return;
            case 'W': set({0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001}); return;
            case 'X': set({0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}); return;
            case 'Y': set({0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}); return;
            case 'Z': set({0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}); return;
            default: break;
        }

        switch(c)
        {
            case '-': set({0, 0, 0, 0b11111, 0, 0, 0}); return;
            case '.': set({0, 0, 0, 0, 0, 0b00100, 0b00100}); return;
            case ':': set({0, 0b00100, 0b00100, 0, 0b00100, 0b00100, 0}); return;
            case '/': set({0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0, 0}); return;
            case '_': set({0, 0, 0, 0, 0, 0, 0b11111}); return;
            case '>': set({0b10000, 0b01000, 0b00100, 0b00010, 0b00100, 0b01000, 0b10000}); return;
            case '<': set({0b00001, 0b00010, 0b00100, 0b01000, 0b00100, 0b00010, 0b00001}); return;
            case '?': set({0b01110, 0b10001, 0b00010, 0b00100, 0b00100, 0, 0b00100}); return;
            default: break;
        }

        set({0b11111, 0b10001, 0b00010, 0b00100, 0b00100, 0, 0b00100});
    }
};

static void DrawTinyString(OledPager& d, const char* str, int x, int y, bool on)
{
    const int char_w = Font5x7::W + 1;
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
                    const int px = x + i * char_w + xx;
                    const int py = y + yy;
                    if(px >= 0 && px < 128 && py >= 0 && py < 64)
                        d.DrawPixel(px, py, on);
                }
            }
        }
    }
}

static int TinyStringWidth(const char* str)
{
    if(str == nullptr || str[0] == '\0')
        return 0;
    const int char_w = Font5x7::W + 1;
    int count = 0;
    for(; str[count] != '\0'; ++count)
    {
    }
    return count * char_w - 1;
}

static void DrawVerticalFadersInRect(OledPager& d,
                                     int x,
                                     int y,
                                     int w,
                                     int h,
                                     const char* const* labels,
                                     const float* values,
                                     int count,
                                     bool select_active,
                                     int selected_index,
                                     const int* x_offsets = nullptr,
                                     const bool* circle_handles = nullptr,
                                     const bool* hide_rails = nullptr,
                                     const bool* hide_handles = nullptr)
{
    if(w <= 2 || h <= 2 || count <= 0)
        return;
    const int label_y = y + h - Font5x7::H - 1;
    int line_top = y + 2;
    int line_bottom = label_y - 2;
    if(line_bottom <= line_top)
        return;

    // Keep edge lanes far enough from bounds so 7px handles stay centered on rails.
    int fader_left = x + 4;
    int fader_right = x + w - 5;
    if(fader_right <= fader_left)
        return;

    const int span_x = fader_right - fader_left;
    const int span_y = line_bottom - line_top;
    for(int f = 0; f < count; ++f)
    {
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
                int lx0 = label_x - 1;
                int lx1 = label_x + label_w;
                int ly0 = label_y - 1;
                int ly1 = label_y + Font5x7::H;
                if(lx0 < x + 1) lx0 = x + 1;
                if(lx1 > x + w - 2) lx1 = x + w - 2;
                if(ly0 < y + 1) ly0 = y + 1;
                if(ly1 > y + h - 2) ly1 = y + h - 2;
                d.DrawRect(lx0, ly0, lx1, ly1, true, true);
                DrawTinyString(d, label, label_x, label_y, false);
            }
            else
            {
                DrawTinyString(d, label, label_x, label_y, true);
            }
        }
    }
}

static constexpr int32_t kMainMenuCount = 3;
static const char* kMenuLabels[kMainMenuCount] = {"PRESETS", "RECORD", "PERFORM"};
static constexpr int32_t kPerformMenuCount = 5;
static const char* kPerformMenuLabels[kPerformMenuCount] = {"ENGINE", "KEYZONE", "ADSR", "EMPHASIS", "PROCESS"};
static constexpr uint32_t kRecordCountdownMs = 4000;
static constexpr int32_t kRecordTargetCount = 2;
static void DrawWaveformPreview(OledPager& d,
                                const Sample& sample,
                                const SampleEdit* edit,
                                int x,
                                int y,
                                int w,
                                int h);

static void DrawCirclePixels(OledPager& d, int cx, int cy, int r, bool on)
{
    if(r <= 0)
        return;
    const int r2 = r * r;
    for(int y = cy - r; y <= cy + r; ++y)
    {
        for(int x = cx - r; x <= cx + r; ++x)
        {
            const int dx = x - cx;
            const int dy = y - cy;
            const int d2 = dx * dx + dy * dy;
            if(d2 >= r2 - r && d2 <= r2 + r)
                d.DrawPixel(x, y, on);
        }
    }
}

static int32_t NextMenuIndex(int32_t current, int32_t delta)
{
    static const int32_t order[kMainMenuCount] = {0, 1, 2};
    int32_t pos = 0;
    for(int32_t i = 0; i < kMainMenuCount; ++i)
    {
        if(order[i] == current)
        {
            pos = i;
            break;
        }
    }
    pos += delta;
    while(pos < 0)
        pos += kMainMenuCount;
    while(pos >= kMainMenuCount)
        pos -= kMainMenuCount;
    return order[pos];
}

static int32_t NextPerformMenuIndex(int32_t current, int32_t delta)
{
    static const int32_t order[kPerformMenuCount] = {0, 1, 2, 3, 4};
    int32_t pos = 0;
    for(int32_t i = 0; i < kPerformMenuCount; ++i)
    {
        if(order[i] == current)
        {
            pos = i;
            break;
        }
    }
    pos += delta;
    while(pos < 0)
        pos += kPerformMenuCount;
    while(pos >= kPerformMenuCount)
        pos -= kPerformMenuCount;
    return order[pos];
}

constexpr int kIconW = 61;
constexpr int kIconH = 29;
constexpr int kIconStride = 8;

static const uint8_t kIconLoadDisk61x29[kIconH * kIconStride] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0,
    0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30,
    0x60, 0x00, 0x3f, 0xff, 0xf8, 0x00, 0x00, 0x30,
    0x60, 0x00, 0x44, 0x01, 0x24, 0x00, 0x00, 0x30,
    0x60, 0x00, 0x84, 0x71, 0x24, 0x00, 0x00, 0x30,
    0x60, 0x21, 0x04, 0x89, 0x3c, 0x04, 0x00, 0x30,
    0x60, 0x61, 0x04, 0xa9, 0x04, 0x0c, 0x00, 0x30,
    0x60, 0xe1, 0x04, 0x89, 0x04, 0x1c, 0x00, 0x30,
    0x61, 0xe1, 0x04, 0x71, 0x04, 0x3c, 0x00, 0x30,
    0x63, 0xe1, 0x03, 0xfe, 0x04, 0x7c, 0x00, 0x30,
    0x67, 0xe1, 0x00, 0x00, 0x04, 0xfc, 0x00, 0x30,
    0x6f, 0xff, 0x3f, 0xff, 0xe5, 0xff, 0xff, 0xf0,
    0x7f, 0xff, 0x40, 0x00, 0x17, 0xff, 0xff, 0xf0,
    0x7f, 0xff, 0x4f, 0xff, 0x97, 0xff, 0xff, 0xf0,
    0x7f, 0xff, 0x40, 0x00, 0x17, 0xff, 0xff, 0xf0,
    0x7f, 0xff, 0x43, 0xfe, 0x17, 0xff, 0xff, 0xf0,
    0x6f, 0xff, 0x40, 0x00, 0x15, 0xff, 0xff, 0xf0,
    0x67, 0xe1, 0x40, 0x00, 0x14, 0xfc, 0x00, 0x30,
    0x63, 0xe1, 0x41, 0xfc, 0x14, 0x7c, 0x00, 0x30,
    0x61, 0xe1, 0x40, 0x00, 0x14, 0x3c, 0x00, 0x30,
    0x60, 0xe1, 0x40, 0x70, 0x14, 0x1c, 0x00, 0x30,
    0x60, 0x61, 0x40, 0x00, 0x14, 0x0c, 0x00, 0x30,
    0x60, 0x21, 0x40, 0x00, 0x14, 0x04, 0x00, 0x30,
    0x60, 0x00, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x30,
    0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30,
    0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30,
    0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t kIconRecordTape61x29[kIconH * kIconStride] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0xbf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe8,
    0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28,
    0xa8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa8,
    0xa4, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xc1, 0x28,
    0xa2, 0x38, 0x00, 0x00, 0x00, 0x00, 0xe2, 0x28,
    0xa1, 0x37, 0xff, 0xff, 0xff, 0xff, 0x64, 0x28,
    0xa0, 0x28, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x28,
    0xa0, 0x28, 0x7f, 0xff, 0xff, 0xf0, 0xa0, 0x28,
    0xa0, 0x28, 0xff, 0xff, 0xff, 0xf8, 0xa0, 0x28,
    0xa0, 0x29, 0xf8, 0x80, 0x08, 0xfc, 0xa0, 0x28,
    0xa0, 0x29, 0xf7, 0x87, 0xef, 0x7c, 0xa0, 0x28,
    0xa0, 0x29, 0xed, 0x83, 0xcd, 0xbc, 0xa0, 0x28,
    0xa0, 0x29, 0xe8, 0xb3, 0xa8, 0xbc, 0xa0, 0x28,
    0xa0, 0x29, 0xed, 0xb3, 0xcd, 0xbc, 0xa0, 0x28,
    0xa0, 0x29, 0xf7, 0x87, 0xef, 0x7c, 0xa0, 0x28,
    0xa0, 0x29, 0xf8, 0x80, 0x08, 0xfc, 0xa0, 0x28,
    0xa0, 0x29, 0xff, 0xff, 0xff, 0xfc, 0xa0, 0x28,
    0xa0, 0x28, 0xfc, 0x00, 0x01, 0xf8, 0xa0, 0x28,
    0xa0, 0x2c, 0x33, 0xff, 0xfe, 0x61, 0xa0, 0x28,
    0xa1, 0x37, 0x77, 0xc0, 0x1f, 0x77, 0x64, 0x28,
    0xa2, 0x38, 0x6f, 0xc0, 0x1f, 0xb0, 0xe2, 0x28,
    0xa4, 0x1f, 0xdf, 0xff, 0xff, 0xdf, 0xc1, 0x28,
    0xa8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa8,
    0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28,
    0xbf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe8,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8,
};

static const uint8_t kIconPerformMpc61x29[kIconH * kIconStride] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0x87, 0xbd, 0xef, 0x7b, 0xcf, 0xff, 0xff, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0x0f, 0xff, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0xef, 0xf0, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0xef, 0xf6, 0xc8,
    0x80, 0x00, 0x00, 0x00, 0x0e, 0xef, 0xf6, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0xef, 0xf6, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0xee, 0x36, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0xee, 0xb6, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0xee, 0xb6, 0xc8,
    0x80, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x86, 0x08,
    0x80, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xc8,
    0x80, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xc8,
    0x89, 0x11, 0x11, 0x20, 0x00, 0x00, 0x00, 0x08,
    0x89, 0x11, 0x11, 0x20, 0x00, 0x00, 0x00, 0x08,
    0x89, 0x11, 0x11, 0x20, 0x00, 0x00, 0x00, 0x08,
    0x89, 0x11, 0x11, 0x20, 0xa0, 0x14, 0x02, 0x88,
    0x89, 0x11, 0x11, 0x21, 0xb0, 0x36, 0x06, 0xc8,
    0x8b, 0xbb, 0xbb, 0xa3, 0xb8, 0x77, 0x0e, 0xe8,
    0x8b, 0xbb, 0xbb, 0xa3, 0xb8, 0x77, 0x0e, 0xe8,
    0x8b, 0xbb, 0xbb, 0xa3, 0xf8, 0x7f, 0x0f, 0xe8,
    0x8b, 0xbb, 0xbb, 0xa1, 0xf0, 0x3e, 0x07, 0xc8,
    0x8f, 0xff, 0xff, 0xe0, 0xe0, 0x1c, 0x03, 0x88,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8,
};

static const uint8_t kIconPerformEngine61x29[kIconH * kIconStride] = {
    
    
    
    
    
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0x00,
    0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00,
    0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x0B, 0x40, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x0B, 0x40, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x0B, 0x40, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x3F, 0xF0, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x01, 0xE0, 0x1E, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x2C, 0x10, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x01, 0xEC, 0x1E, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x20, 0x10, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x01, 0xE0, 0x1E, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x20, 0x10, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x01, 0xE0, 0x1E, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x3F, 0xF0, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x0B, 0x40, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x0B, 0x40, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x0B, 0x40, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
    0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0x00,
    0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
    0x00, 0x80, 0x00, 0x3F, 0xF0, 0x00, 0x08, 0x00,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,





};

static const uint8_t kIconPerformKeyzone61x29[kIconH * kIconStride] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0xBD, 0xEE, 0x03, 0xD5, 0xFF, 0xFF, 0xFF, 0xE8,
    0xA1, 0x01, 0x02, 0x14, 0x00, 0x00, 0x00, 0x28,
    0xA1, 0x02, 0x03, 0xDC, 0x00, 0x00, 0x00, 0x28,
    0xA1, 0x04, 0x7A, 0x04, 0x00, 0x00, 0x00, 0x28,
    0xA1, 0x08, 0x02, 0x04, 0x00, 0x00, 0x00, 0x28,
    0xA1, 0xEF, 0x03, 0xC4, 0x00, 0x00, 0x00, 0x28,
    0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28,
    0xBF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE8,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0xF0, 0x78, 0x38, 0x3F, 0xFF, 0xFF, 0xFF, 0xF8,
    0xF0, 0x78, 0x38, 0x3F, 0xFF, 0xFF, 0xFF, 0xF8,
    0xF0, 0x78, 0x38, 0x3C, 0x7F, 0xFF, 0xFF, 0xF8,
    0xF0, 0x78, 0x38, 0x3B, 0x3F, 0xFF, 0xFF, 0xF8,
    0xF0, 0x78, 0x38, 0x36, 0xB8, 0x7F, 0xFF, 0xF8,
    0xF0, 0x78, 0x38, 0x35, 0x8B, 0x7F, 0xFF, 0xF8,
    0xF0, 0x78, 0x38, 0x36, 0x73, 0x00, 0x00, 0x78,
    0xF0, 0x78, 0x38, 0x3A, 0xBC, 0xFF, 0xFF, 0xB8,
    0xF0, 0x78, 0x38, 0x3A, 0xBC, 0xFF, 0xFF, 0xB8,
    0xF0, 0x78, 0x38, 0x36, 0x73, 0x00, 0x35, 0xB8,
    0xF0, 0x78, 0x38, 0x35, 0x8B, 0x7E, 0xE4, 0x38,
    0xFD, 0xFE, 0xFE, 0xF6, 0xB8, 0x7E, 0xD1, 0xF8,
    0xFD, 0xFE, 0xFE, 0xFB, 0x3F, 0xFE, 0xDF, 0xF8,
    0xFD, 0xFE, 0xFE, 0xFC, 0x7F, 0xFF, 0x1F, 0xF8,
    0xFD, 0xFE, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8,
    0xFD, 0xFE, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8,
    0xFD, 0xFE, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8,
    0xFD, 0xFE, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8,
};

static const uint8_t kIconPerformAdsr61x29[kIconH * kIconStride] = {
    
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x20, 0xC0, 0x20, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x21, 0xE0, 0x20, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x22, 0xD0, 0x20, 0x40, 0x08, 0x00, 0x00, 0x00,
    0x20, 0xC0, 0x20, 0xA0, 0x08, 0x00, 0x00, 0x00,
    0x20, 0xC0, 0x21, 0x10, 0x08, 0x00, 0x00, 0x00,
    0x20, 0xC0, 0x22, 0x08, 0x08, 0x00, 0x00, 0x00,
    0x20, 0xC0, 0x24, 0x04, 0x08, 0x00, 0x00, 0x00,
    0x20, 0xC0, 0x28, 0x02, 0x08, 0x00, 0x00, 0x00,
    0x20, 0xC0, 0x30, 0x01, 0x08, 0x00, 0x00, 0x00,
    0x22, 0xD0, 0x20, 0x00, 0x88, 0x00, 0x00, 0x00,
    0x21, 0xE0, 0x60, 0x00, 0x48, 0x00, 0x00, 0x00,
    0x20, 0xC0, 0xA0, 0x00, 0x3F, 0xFF, 0x00, 0x00,
    0x3F, 0xFF, 0xE0, 0x00, 0x08, 0x00, 0x80, 0x00,
    0x20, 0x02, 0x20, 0x00, 0x08, 0x00, 0x40, 0x00,
    0x20, 0x04, 0x20, 0x00, 0x08, 0x00, 0x20, 0x00,
    0x20, 0x08, 0x22, 0x00, 0x88, 0x00, 0x10, 0x00,
    0x20, 0x10, 0x24, 0x00, 0x48, 0x00, 0x08, 0x00,
    0x20, 0x20, 0x2F, 0xFF, 0xE8, 0x00, 0x04, 0x00,
    0x20, 0x40, 0x2F, 0xFF, 0xE8, 0x00, 0x02, 0x00,
    0x20, 0x80, 0x24, 0x00, 0x48, 0x00, 0x01, 0x00,
    0x21, 0x00, 0x22, 0x00, 0x88, 0x00, 0x00, 0x80,
    0x22, 0x00, 0x20, 0x00, 0x08, 0x00, 0x00, 0x40,
    0x24, 0x00, 0x20, 0x00, 0x08, 0x00, 0x00, 0x20,
    0x28, 0x00, 0x20, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x30, 0x00, 0x20, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

};

static const uint8_t kIconPerformEmphasis61x29[kIconH * kIconStride] = {
    
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x3F, 0xF8, 0x00, 0x00, 0x40, 0x00, 0x00,
    0x00, 0x40, 0x04, 0x07, 0xFF, 0x67, 0xFF, 0xE0,
    0x00, 0x9F, 0xF2, 0x04, 0x00, 0x50, 0x00, 0x20,
    0x00, 0xA0, 0x0A, 0x04, 0x00, 0x50, 0xFE, 0x20,
    0x00, 0xA0, 0x0A, 0x04, 0x01, 0xC0, 0xFE, 0x20,
    0x00, 0xA0, 0x0A, 0x04, 0x03, 0xC0, 0x82, 0x20,
    0x20, 0xA0, 0x0A, 0x04, 0x41, 0x80, 0x82, 0x20,
    0x10, 0xA0, 0x0A, 0x04, 0x60, 0x00, 0x82, 0x20,
    0x09, 0x50, 0x15, 0x00, 0x50, 0x03, 0x8E, 0x00,
    0x02, 0x53, 0x94, 0x80, 0x50, 0x07, 0x9E, 0x00,
    0x02, 0x57, 0xD4, 0x81, 0xC0, 0x03, 0x0C, 0x00,
    0x02, 0x54, 0x54, 0x83, 0xC0, 0x00, 0x00, 0x00,
    0x02, 0x57, 0xD4, 0x81, 0x80, 0x0F, 0xFF, 0xE0,
    0x09, 0x54, 0x55, 0x00, 0x00, 0x08, 0x20, 0x20,
    0x10, 0xD7, 0xD6, 0x10, 0x04, 0x0E, 0x20, 0x20,
    0x20, 0x04, 0x40, 0x18, 0x06, 0x08, 0xE0, 0x20,
    0x00, 0x07, 0xC0, 0x14, 0x05, 0x09, 0xE0, 0x20,
    0x00, 0x37, 0xD8, 0x14, 0x05, 0x08, 0xC0, 0xE0,
    0x00, 0x33, 0x98, 0x70, 0x1C, 0x08, 0x01, 0xE0,
    0x00, 0x18, 0x30, 0xF0, 0x3C, 0x08, 0x00, 0xC0,
    0x00, 0x0F, 0xE0, 0x60, 0x18, 0x38, 0x00, 0x00,
    0x00, 0x03, 0x80, 0x00, 0x00, 0x78, 0x80, 0x80,
    0x00, 0x03, 0x80, 0x00, 0x00, 0x30, 0x7F, 0x00,
    0x00, 0x0F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

};

static const uint8_t kIconPerformProcess61x29[kIconH * kIconStride] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0xFF, 0x00, 0x00, 0x00, 0x07, 0xFF, 0x00,
    0x09, 0xAF, 0x8F, 0xFF, 0xFF, 0x89, 0xAF, 0x80,
    0x0B, 0x27, 0x90, 0x00, 0x00, 0x4B, 0x27, 0x80,
    0x0B, 0x07, 0x93, 0x87, 0x0E, 0x4B, 0x07, 0x80,
    0x0B, 0x07, 0x97, 0xCE, 0x9F, 0x4B, 0x07, 0x80,
    0x0B, 0x8F, 0x96, 0xCD, 0x87, 0x4B, 0x8F, 0x80,
    0x0B, 0xFF, 0x95, 0xCF, 0x9F, 0x4B, 0xFF, 0x80,
    0x0B, 0xFF, 0x93, 0x87, 0x0E, 0x4B, 0xFF, 0x80,
    0x0B, 0xFF, 0x90, 0x00, 0x00, 0x4B, 0xFF, 0x80,
    0x0B, 0xFF, 0x8F, 0xFF, 0xFF, 0x8B, 0xFF, 0x80,
    0x0B, 0xFF, 0x80, 0x5D, 0xD0, 0x0B, 0xFF, 0x80,
    0x1F, 0xAF, 0xC0, 0x30, 0x60, 0x1F, 0xAF, 0xC0,
    0x2F, 0x27, 0xA0, 0x20, 0x40, 0x2F, 0x27, 0xA0,
    0x3F, 0x07, 0xF0, 0x20, 0x40, 0x7F, 0x07, 0xE0,
    0x1F, 0x06, 0xD0, 0x20, 0x40, 0x5F, 0x06, 0xC0,
    0x0F, 0x8E, 0x90, 0x20, 0x40, 0x4F, 0x8E, 0x80,
    0x0F, 0xFE, 0x90, 0x20, 0x40, 0x4F, 0xFE, 0x80,
    0x0F, 0xFE, 0x90, 0x20, 0x40, 0x4F, 0xFE, 0x80,
    0x0F, 0xFE, 0x90, 0x20, 0x40, 0x4F, 0xFE, 0x80,
    0x0F, 0xFE, 0x90, 0x20, 0x40, 0x4F, 0xFE, 0x80,
    0x0F, 0x8E, 0x90, 0x20, 0x40, 0x4F, 0x8E, 0x80,
    0x0F, 0x76, 0x88, 0x40, 0x20, 0x8F, 0x76, 0x80,
    0x0F, 0x76, 0x87, 0x80, 0x1F, 0x0F, 0x76, 0x80,
    0x0F, 0x8C, 0x80, 0x00, 0x00, 0x0F, 0x8C, 0x80,
    0x07, 0xFF, 0x00, 0x00, 0x00, 0x07, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

};

static void DrawBitmap1bpp(OledPager& disp,
                           int x,
                           int y,
                           int w,
                           int h,
                           int stride,
                           const uint8_t* data,
                           bool on)
{
    for(int row = 0; row < h; ++row)
    {
        const uint8_t* line = data + (row * stride);
        for(int col = 0; col < w; ++col)
        {
            const uint8_t byte = line[col >> 3];
            const int bit = 7 - (col & 7);
            if((byte >> bit) & 0x1)
                disp.DrawPixel(x + col, y + row, on);
        }
    }
}

static void DrawMainMenuFriendStyle(OledPager& d, int selected)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;
    constexpr int kListLeftX = 2;
    constexpr int kListGapY = 6;

    d.Fill(false);

    const int text_h = Font5x7::H;
    int max_label_w = 0;
    for(int32_t i = 0; i < kMainMenuCount; ++i)
    {
        const int w = TinyStringWidth(kMenuLabels[i]);
        if(w > max_label_w)
            max_label_w = w;
    }
    const int total_h = (kMainMenuCount * text_h) + ((kMainMenuCount - 1) * kListGapY);
    const int start_y = (kDisplayH - total_h) / 2;
    const int list_w = max_label_w;
    const int icon_area_x = list_w + 4;
    const int icon_area_w = kDisplayW - icon_area_x;

    for(int32_t i = 0; i < kMainMenuCount; ++i)
    {
        const bool is_selected = (i == selected);
        const char* label = kMenuLabels[i];
        const int text_x = kListLeftX;
        const int text_y = start_y + i * (text_h + kListGapY);
        if(is_selected)
        {
            const int pad = 1;
            int rect_x0 = text_x - pad;
            int rect_y0 = text_y - pad;
            int rect_x1 = text_x + list_w + pad;
            int rect_y1 = text_y + text_h + pad;
            if(rect_x0 < 0) rect_x0 = 0;
            if(rect_y0 < 0) rect_y0 = 0;
            if(rect_x1 >= kDisplayW) rect_x1 = kDisplayW - 1;
            if(rect_y1 >= kDisplayH) rect_y1 = kDisplayH - 1;
            d.DrawRect(rect_x0,
                       rect_y0,
                       rect_x1,
                       rect_y1,
                       true,
                       true);
            DrawTinyString(d, label, text_x, text_y, false);
        }
        else
        {
            DrawTinyString(d, label, text_x, text_y, true);
        }

        if(is_selected)
        {
            const uint8_t* icon = nullptr;
            int icon_w = 0;
            int icon_h = 0;
            int icon_stride = 0;
            if(i == 0)
            {
                icon = kIconLoadDisk61x29;
                icon_w = kIconW;
                icon_h = kIconH;
                icon_stride = kIconStride;
            }
            else if(i == 1)
            {
                icon = kIconRecordTape61x29;
                icon_w = kIconW;
                icon_h = kIconH;
                icon_stride = kIconStride;
            }
            else if(i == 2)
            {
                icon = kIconPerformMpc61x29;
                icon_w = kIconW;
                icon_h = kIconH;
                icon_stride = kIconStride;
            }
            if(icon != nullptr && icon_area_w > icon_w)
            {
                const int icon_x = icon_area_x + (icon_area_w - icon_w) / 2;
                const int icon_y = (kDisplayH - icon_h) / 2;
                DrawBitmap1bpp(d,
                               icon_x,
                               icon_y,
                               icon_w,
                               icon_h,
                               icon_stride,
                               icon,
                               true);
            }
        }
    }
}

static void DrawPerformMenuFriendStyle(OledPager& d, int selected)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;
    constexpr int kListLeftX = 2;
    constexpr int kListGapY = 6;

    d.Fill(false);

    const int text_h = Font5x7::H;
    int max_label_w = 0;
    for(int32_t i = 0; i < kPerformMenuCount; ++i)
    {
        const int w = TinyStringWidth(kPerformMenuLabels[i]);
        if(w > max_label_w)
            max_label_w = w;
    }
    const int total_h = (kPerformMenuCount * text_h) + ((kPerformMenuCount - 1) * kListGapY);
    const int start_y = (kDisplayH - total_h) / 2;
    const int list_w = max_label_w;
    const int icon_area_x = list_w + 4;
    const int icon_area_w = kDisplayW - icon_area_x;

    for(int32_t i = 0; i < kPerformMenuCount; ++i)
    {
        const bool is_selected = (i == selected);
        const char* label = kPerformMenuLabels[i];
        const int text_x = kListLeftX;
        const int text_y = start_y + i * (text_h + kListGapY);
        if(is_selected)
        {
            const int pad = 1;
            int rect_x0 = text_x - pad;
            int rect_y0 = text_y - pad;
            int rect_x1 = text_x + list_w + pad;
            int rect_y1 = text_y + text_h + pad;
            if(rect_x0 < 0) rect_x0 = 0;
            if(rect_y0 < 0) rect_y0 = 0;
            if(rect_x1 >= kDisplayW) rect_x1 = kDisplayW - 1;
            if(rect_y1 >= kDisplayH) rect_y1 = kDisplayH - 1;
            d.DrawRect(rect_x0,
                       rect_y0,
                       rect_x1,
                       rect_y1,
                       true,
                       true);
            DrawTinyString(d, label, text_x, text_y, false);
        }
        else
        {
            DrawTinyString(d, label, text_x, text_y, true);
        }

        if(is_selected)
        {
            const uint8_t* icon = nullptr;
            int icon_w = 0;
            int icon_h = 0;
            int icon_stride = 0;
            if(i == 0)
            {
                icon = kIconPerformEngine61x29;
                icon_w = kIconW;
                icon_h = kIconH;
                icon_stride = kIconStride;
            }
            else if(i == 1)
            {
                icon = kIconPerformKeyzone61x29;
                icon_w = kIconW;
                icon_h = kIconH;
                icon_stride = kIconStride;
            }
            else if(i == 2)
            {
                icon = kIconPerformAdsr61x29;
                icon_w = kIconW;
                icon_h = kIconH;
                icon_stride = kIconStride;
            }
            else if(i == 3)
            {
                icon = kIconPerformEmphasis61x29;
                icon_w = kIconW;
                icon_h = kIconH;
                icon_stride = kIconStride;
            }
            else if(i == 4)
            {
                icon = kIconPerformProcess61x29;
                icon_w = kIconW;
                icon_h = kIconH;
                icon_stride = kIconStride;
            }
            if(icon != nullptr && icon_area_w > icon_w)
            {
                const int icon_x = icon_area_x + (icon_area_w - icon_w) / 2;
                const int icon_y = (kDisplayH - icon_h) / 2;
                DrawBitmap1bpp(d,
                               icon_x,
                               icon_y,
                               icon_w,
                               icon_h,
                               icon_stride,
                               icon,
                               true);
            }
        }
    }
}

static bool MainMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        const int32_t next = NextMenuIndex(static_cast<int32_t>(ctx.app->main_menu_index), e.value);
        ctx.app->main_menu_index = static_cast<uint8_t>(next);
        ctx.app->ui_dirty = true;
        return true;
    }

    return false;
}

static bool MainMenu_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return false;

    const uint8_t selected = static_cast<uint8_t>(ctx.app->main_menu_index % kMainMenuCount);
    switch(selected)
    {
        case 0:
            return UiNav_Push(ctx.app->ui_nav, UiScreenId::Presets);
        case 1:
            return UiNav_Push(ctx.app->ui_nav, UiScreenId::Record);
        case 2:
        default:
            return UiNav_Push(ctx.app->ui_nav, UiScreenId::PerformMenu);
    }
}

static void MainMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const int selected = static_cast<int>(ctx.app->main_menu_index % kMainMenuCount);
    DrawMainMenuFriendStyle(*ctx.display, selected);
}



// HOME -> PRESETS (blank placeholder screen for now)
static bool Presets_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    (void)ctx;
    (void)e;
    // Do not consume BACK events; let the router/nav handle it.
    return false;
}

static void Presets_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    UiDraw_Header(d, layout, "PRESETS", status);

    // Blank body for now (future: list of saved presets).
}

static void Record_ResetLiveWave(AppState& app)
{
    for(int i = 0; i < 128; ++i)
    {
        app.rec_live_min[i] = 0;
        app.rec_live_max[i] = 0;
    }
    app.rec_live_last_col = -1;
}

static void Record_StopPreview(AppState& app)
{
    app.record_preview_hold = false;
    app.record_preview_gate = false;
}

static void Record_PrepareRecordingUiState(AppState& app)
{
    Record_StopPreview(app);
    app.rec_monitor_enable.store(0, std::memory_order_release);
    app.rec_start_req.store(0, std::memory_order_release);
    app.rec_stop_req.store(0, std::memory_order_release);
    app.rec_active.store(0, std::memory_order_release);
    app.rec_pos.store(0, std::memory_order_release);
    app.rec_length.store(0, std::memory_order_release);
    Record_ResetLiveWave(app);

    const uint8_t slot = app.perform_layer & 1u;
    app.record_slot = slot;
    app.rec_slot_pending.store(slot, std::memory_order_release);

    // Reset target slot metadata so review/save reflects the fresh unsaved take.
    Sample& s = app.sd_slots[slot];
    s.pcm = nullptr;
    s.length = 0;
    s.sample_rate = 48000;
    s.root_key = 60;
    s.loop_start = 0;
    s.loop_end = 0;
    s.loop_enabled = false;

    SampleEdit edit = SampleEdit_Default(0);
    app.sd_edit_slots[slot] = edit;
    app.sd_edit_pending = edit;
    app.sd_edit_slot.store(slot, std::memory_order_release);
    app.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
    app.sd_edit_ready.store(1, std::memory_order_release);
}

static void Record_StartRecording(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;

    AppState& app = *ctx.app;
    const uint8_t src = (app.record_source_index == 1)
                            ? static_cast<uint8_t>(RecordInputSource::Mic)
                            : static_cast<uint8_t>(RecordInputSource::LineIn);
    app.rec_source_sel.store(src, std::memory_order_release);
    Record_PrepareRecordingUiState(app);
    // Keep input monitor live while recording.
    app.rec_monitor_enable.store(1, std::memory_order_release);
    app.rec_start_req.store(1, std::memory_order_release);
    app.record_state = RecordUiState::Recording;
    app.ui_dirty = true;
}

static void Record_RenderReadyCuzStyle(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;
    AppState& app = *ctx.app;
    OledPager& d = *ctx.display;

    static uint8_t text_mask[64][128];
    static uint8_t text_fb[64][128];
    static uint8_t bold_fb[64][128];
    static uint8_t fb_buf[64][128];
    static uint8_t cached_source = 0xFFu;
    static bool cache_valid = false;

    if(app.record_anim_start_ms < 0.0)
        app.record_anim_start_ms = static_cast<double>(ctx.now_ms);

    const double elapsed_s = (static_cast<double>(ctx.now_ms) - app.record_anim_start_ms) / 1000.0;
    const int cx = 64;
    const int cy = 32;

    const char* line1 = (app.record_source_index == 1) ? "RECORD MICROPHONE" : "RECORD LINE IN";
    const char* line2 = "READY";
    int scale = 2;
    int char_spacing = scale;
    int line_gap = scale * 2;
    int char_h = Font5x7::H * scale;
    const int lines = 2;
    int text_h = lines * char_h + (lines - 1) * line_gap;
    int y0 = (64 / 2) - (text_h / 2);

    if(!cache_valid || cached_source != app.record_source_index)
    {
        std::memset(text_mask, 0, sizeof(text_mask));
        std::memset(text_fb, 0, sizeof(text_fb));
        std::memset(bold_fb, 0, sizeof(bold_fb));

        auto mark_char = [&](int x, int y, char c)
        {
            uint8_t rows[Font5x7::H] = {};
            Font5x7::GetGlyphRows(c, rows);
            for(int yy = 0; yy < Font5x7::H; ++yy)
            {
                uint8_t row = rows[yy];
                for(int xx = 0; xx < Font5x7::W; ++xx)
                {
                    if(((row >> (Font5x7::W - 1 - xx)) & 1u) == 0u)
                        continue;
                    for(int sy = 0; sy < scale; ++sy)
                    {
                        for(int sx = 0; sx < scale; ++sx)
                        {
                            const int px = x + xx * scale + sx;
                            const int py = y + yy * scale + sy;
                            if(px >= 0 && px < 128 && py >= 0 && py < 64)
                            {
                                text_mask[py][px] = 1;
                                text_fb[py][px] = 1;
                            }
                        }
                    }
                }
            }
        };

        auto mark_line = [&](int x, int y, const char* text)
        {
            const int char_w = Font5x7::W * scale;
            int cx0 = x;
            for(const char* p = text; *p; ++p)
            {
                mark_char(cx0, y, *p);
                cx0 += char_w + char_spacing;
            }
        };

        auto width = [&](const char* t)
        {
            const int len = static_cast<int>(std::strlen(t));
            if(len <= 0)
                return 0;
            const int char_w = Font5x7::W * scale;
            return len * char_w + (len - 1) * char_spacing;
        };

        int max_w = width(line1);
        const int line2_w = width(line2);
        if(line2_w > max_w)
            max_w = line2_w;
        if(max_w > 128)
        {
            scale = 1;
            char_spacing = scale;
            line_gap = scale * 2;
            char_h = Font5x7::H * scale;
        }
        text_h = lines * char_h + (lines - 1) * line_gap;
        y0 = (64 / 2) - (text_h / 2);

        auto mark_centered = [&](const char* t1, const char* t2)
        {
            auto width2 = [&](const char* t)
            {
                const int len = static_cast<int>(std::strlen(t));
                if(len <= 0)
                    return 0;
                const int char_w = Font5x7::W * scale;
                return len * char_w + (len - 1) * char_spacing;
            };
            const int w1 = width2(t1);
            const int w2 = width2(t2);
            const int x1 = (128 / 2) - (w1 / 2);
            const int x2 = (128 / 2) - (w2 / 2);
            mark_line(x1, y0, t1);
            mark_line(x2, y0 + char_h + line_gap, t2);
        };

        mark_centered(line1, line2);

        // Precompute bold version of the text-only buffer.
        for(int y = 0; y < 64; ++y)
        {
            for(int x = 0; x < 128; ++x)
            {
                if(!text_mask[y][x])
                    continue;
                for(int dy = 0; dy <= 1; ++dy)
                {
                    for(int dx = 0; dx <= 1; ++dx)
                    {
                        const int px = x + dx;
                        const int py = y + dy;
                        if(px >= 0 && px < 128 && py >= 0 && py < 64)
                            bold_fb[py][px] = 1;
                    }
                }
            }
        }
        cache_valid = true;
        cached_source = app.record_source_index;
    }

    const double max_visible_r = std::sqrt(std::pow(128 / 2.0, 2) + std::pow(64 / 2.0, 2));
    const double start_r = max_visible_r + 10.0;
    const double duration_s = 1.0;
    const double offset1_s = 0.2;
    const double offset2_s = offset1_s + 0.3;
    const double grow_duration_s = 0.5;
    const double grow_start_s = offset2_s + duration_s / 2.0;
    const double gap_s = 0.1;
    const double cycle_s = grow_start_s + grow_duration_s + gap_s;
    const double anim_t = std::fmod(elapsed_s, cycle_s);
    const double flicker_on_s = 0.1;
    const double flicker_off_s = 0.1;
    const double flicker_period = flicker_on_s + flicker_off_s;
    const double flicker_phase = std::fmod(anim_t, flicker_period);
    const bool flicker_on = (flicker_phase < flicker_on_s);
    std::memcpy(fb_buf, flicker_on ? bold_fb : text_fb, sizeof(fb_buf));

    auto for_circle_perimeter = [&](int r, const auto& fn)
    {
        if(r <= 0)
            return;
        auto plot_if_in = [&](int px, int py)
        {
            if(px >= 0 && px < 128 && py >= 0 && py < 64)
                fn(px, py);
        };
        int x = r;
        int y = 0;
        int err = 1 - x;
        while(x >= y)
        {
            plot_if_in(cx + x, cy + y);
            plot_if_in(cx + y, cy + x);
            plot_if_in(cx - y, cy + x);
            plot_if_in(cx - x, cy + y);
            plot_if_in(cx - x, cy - y);
            plot_if_in(cx - y, cy - x);
            plot_if_in(cx + y, cy - x);
            plot_if_in(cx + x, cy - y);
            ++y;
            if(err < 0)
                err += 2 * y + 1;
            else
            {
                --x;
                err += 2 * (y - x) + 1;
            }
        }
    };

    auto animate_circle = [&](double t_offset,
                              int thickness_px,
                              bool invert_text,
                              double speedup_after_abs = -1.0,
                              double speedup_factor = 1.0)
    {
        const double local_t = anim_t - t_offset;
        if(local_t < 0.0 || local_t > duration_s)
            return;
        double adj_local_t = local_t;
        if(speedup_after_abs >= 0.0 && speedup_factor != 1.0)
        {
            const double threshold_local = speedup_after_abs - t_offset;
            if(local_t > threshold_local)
            {
                const double extra = local_t - threshold_local;
                adj_local_t = threshold_local + extra * speedup_factor;
                if(adj_local_t > duration_s)
                    adj_local_t = duration_s;
            }
        }

        const double f = 1.0 - (adj_local_t / duration_s);
        const double r = start_r * f;
        if(r > max_visible_r)
            return;
        const int ri = static_cast<int>(std::round(r));
        for(int t = 0; t < thickness_px; ++t)
        {
            const int rr = ri - t;
            if(rr <= 0)
                continue;
            for_circle_perimeter(rr, [&](int px, int py)
            {
                if(text_mask[py][px] && invert_text)
                    fb_buf[py][px] = !fb_buf[py][px];
                else if(!text_mask[py][px])
                    fb_buf[py][px] = 1;
            });
        }
    };

    animate_circle(0.0, 2, false);
    animate_circle(offset1_s, 4, true);
    animate_circle(offset2_s, 2, false, offset1_s + duration_s, 2.0);

    auto animate_grow_circle = [&](double t_offset, int thickness_px)
    {
        const double local_t = anim_t - t_offset;
        if(local_t < 0.0 || local_t > grow_duration_s)
            return;
        const double f = local_t / grow_duration_s;
        const double base_r = thickness_px - 1;
        const double target_r = max_visible_r + thickness_px - 1;
        const double r = base_r + f * (target_r - base_r);
        const int ri = static_cast<int>(std::round(r));
        for(int t = 0; t < thickness_px; ++t)
        {
            const int rr = ri - t;
            if(rr <= 0)
                continue;
            for_circle_perimeter(rr, [&](int px, int py)
            {
                if(text_mask[py][px])
                    fb_buf[py][px] = !fb_buf[py][px];
                else
                    fb_buf[py][px] = !fb_buf[py][px];
            });
        }
    };
    animate_grow_circle(grow_start_s, 16);

    for(int y = 0; y < 64; ++y)
    {
        for(int x = 0; x < 128; ++x)
            d.DrawPixel(x, y, fb_buf[y][x] != 0);
    }
}

static bool Record_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    AppState& app = *ctx.app;
    if(ctx.shift)
        return false;

    auto wrap2 = [](int v) -> uint8_t
    {
        while(v < 0)
            v += 2;
        while(v >= 2)
            v -= 2;
        return static_cast<uint8_t>(v);
    };

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        if(app.record_state == RecordUiState::SourceSelect)
        {
            app.record_source_index = wrap2(static_cast<int>(app.record_source_index) + e.value);
            app.rec_source_sel.store(app.record_source_index & 1u, std::memory_order_release);
            app.ui_dirty = true;
            return true;
        }
        if(app.record_state == RecordUiState::TargetSelect)
        {
            app.record_target_index = wrap2(static_cast<int>(app.record_target_index) + e.value);
            app.ui_dirty = true;
            return true;
        }
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        if(app.record_state == RecordUiState::Review)
        {
            app.record_state = RecordUiState::BackConfirm;
            Record_StopPreview(app);
            app.ui_dirty = true;
            return true;
        }
        if(app.record_state == RecordUiState::BackConfirm)
        {
            app.record_state = RecordUiState::Review;
            app.ui_dirty = true;
            return true;
        }
        if(app.record_state == RecordUiState::Recording)
        {
            app.rec_stop_req.store(1, std::memory_order_release);
            app.ui_dirty = true;
            return true;
        }
        if(app.record_state == RecordUiState::Armed)
        {
            app.record_state = RecordUiState::SourceSelect;
            app.rec_monitor_enable.store(0, std::memory_order_release);
            app.record_anim_start_ms = -1.0;
            app.ui_dirty = true;
            return true;
        }
        if(app.record_state == RecordUiState::TargetSelect)
        {
            app.record_state = RecordUiState::Review;
            app.ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        if(app.record_state == RecordUiState::Review)
        {
            app.record_preview_hold = true;
            app.ui_dirty = true;
            return true;
        }
    }
    else if(e.type == UiInputType::BtnUp && e.id == kUiBtnPod2)
    {
        if(app.record_state == RecordUiState::Review)
        {
            Record_StopPreview(app);
            app.ui_dirty = true;
            return true;
        }
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(app.record_state == RecordUiState::SourceSelect)
        {
            app.record_state = RecordUiState::Armed;
            app.rec_source_sel.store(app.record_source_index & 1u, std::memory_order_release);
            app.rec_monitor_enable.store(1, std::memory_order_release);
            app.record_anim_start_ms = -1.0;
            app.ui_dirty = true;
            return true;
        }
        if(app.record_state == RecordUiState::Armed)
        {
            app.record_countdown_start_ms = ctx.now_ms;
            app.record_state = RecordUiState::Countdown;
            app.rec_monitor_enable.store(1, std::memory_order_release);
            app.record_anim_start_ms = -1.0;
            app.ui_dirty = true;
            return true;
        }
        if(app.record_state == RecordUiState::Recording)
        {
            app.rec_stop_req.store(1, std::memory_order_release);
            app.ui_dirty = true;
            return true;
        }
        if(app.record_state == RecordUiState::Review)
        {
            app.record_target_index = 0;
            app.record_state = RecordUiState::TargetSelect;
            app.ui_dirty = true;
            return true;
        }
        if(app.record_state == RecordUiState::TargetSelect)
        {
            if(app.record_target_index == 0)
            {
                UiReq req{UiReqType::SaveRenderedWavCurrent, 0, 0};
                if(UiReq_Push(app, req))
                {
                    SdBrowser_SetSaveStatus(app.sd, "SAVING");
                    app.sd.save_progress = 0;
                    app.sd.save_in_progress = true;
                    app.record_state = RecordUiState::SaveWait;
                }
                else
                {
                    SdBrowser_SetSaveStatus(app.sd, "SAVE ERR");
                    app.record_state = RecordUiState::Review;
                }
            }
            else
            {
                Record_StopPreview(app);
                app.record_state = RecordUiState::SourceSelect;
            }
            app.ui_dirty = true;
            return true;
        }
        if(app.record_state == RecordUiState::BackConfirm)
        {
            Record_StopPreview(app);
            app.rec_stop_req.store(1, std::memory_order_release);
            app.rec_active.store(0, std::memory_order_release);
            app.rec_length.store(0, std::memory_order_release);
            app.rec_monitor_enable.store(0, std::memory_order_release);
            app.record_state = RecordUiState::SourceSelect;
            app.ui_dirty = true;
            return true;
        }
    }

    return false;
}

static void Record_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));

    const uint32_t rec_len = app.rec_length.load(std::memory_order_acquire);
    const uint32_t rec_pos = app.rec_pos.load(std::memory_order_acquire);

    if(app.record_state == RecordUiState::Countdown)
    {
        const uint32_t elapsed = ctx.now_ms - app.record_countdown_start_ms;
        if(elapsed >= kRecordCountdownMs)
        {
            Record_StartRecording(ctx);
        }
    }

    if(app.record_state == RecordUiState::SaveWait)
    {
        if(!app.sd.save_in_progress && !app.ui_req_busy)
        {
            const bool save_ok = (std::strncmp(app.sd.save_status, "SAVED", 5) == 0);
            if(save_ok)
            {
                // Successful save: go back to source select (recording is no longer at risk).
                Record_StopPreview(app);
                app.rec_monitor_enable.store(0, std::memory_order_release);
                app.record_state = RecordUiState::SourceSelect;
            }
            else
            {
                // On failure, return to review so user can retry/save again.
                app.record_state = RecordUiState::Review;
            }
            app.ui_dirty = true;
        }
    }

    switch(app.record_state)
    {
        case RecordUiState::SourceSelect:
        {
            UiDraw_Header(d, layout, "SOURCE", status);
            const int line_h = layout.line_h + 2;
            const int y0 = layout.y_body + 2;
            const char* names[2] = {"LINE IN", "MICROPHONE"};
            for(int i = 0; i < 2; ++i)
            {
                const int y = y0 + i * line_h;
                const bool sel = (static_cast<uint8_t>(i) == app.record_source_index);
                if(sel)
                    d.DrawRect(0, y, 127, y + line_h - 1, true, true);
                d.SetCursor(2, y + 1);
                d.WriteString(names[i], Font_6x8, !sel);
            }
        }
        break;

        case RecordUiState::Armed:
        {
            Record_RenderReadyCuzStyle(ctx);
        }
        break;

        case RecordUiState::Countdown:
        {
            const uint32_t elapsed = ctx.now_ms - app.record_countdown_start_ms;
            const uint32_t remain = (elapsed < kRecordCountdownMs) ? (kRecordCountdownMs - elapsed) : 0u;
            const uint32_t sec = (remain + 999u) / 1000u;
            const int cx = 64;
            const int cy = 32;
            const int outer_r = 30;
            const int inner_r = 22;

            auto draw_scaled_char = [&](char ch, int x, int y, int scale)
            {
                if(ch < 32 || ch > 126 || scale <= 0)
                    return;
                const uint32_t base = static_cast<uint32_t>(ch - 32) * Font_6x8.FontHeight;
                for(uint32_t row = 0; row < Font_6x8.FontHeight; ++row)
                {
                    const uint32_t bits = Font_6x8.data[base + row];
                    for(uint32_t col = 0; col < Font_6x8.FontWidth; ++col)
                    {
                        const bool pixel_on = ((bits << col) & 0x8000u) != 0u;
                        if(!pixel_on)
                            continue;
                        const int px = x + static_cast<int>(col * scale);
                        const int py = y + static_cast<int>(row * scale);
                        for(int dy = 0; dy < scale; ++dy)
                        {
                            for(int dx = 0; dx < scale; ++dx)
                            {
                                d.DrawPixel(px + dx, py + dy, true);
                            }
                        }
                    }
                }
            };

            DrawCirclePixels(d, cx, cy, outer_r, true);
            DrawCirclePixels(d, cx, cy, inner_r, true);
            d.DrawLine(cx, 0, cx, 63, true);
            d.DrawLine(0, cy, 127, cy, true);

            const float phase = static_cast<float>(elapsed % 1000u) / 1000.0f;
            const float angle = phase * 2.0f * 3.14159265f;
            const int hand_r = outer_r - 2;
            const int hx = cx + static_cast<int>(std::cos(angle) * hand_r);
            const int hy = cy + static_cast<int>(std::sin(angle) * hand_r);
            d.DrawLine(cx, cy, hx, hy, true);

            char big[8];
            std::snprintf(big, sizeof(big), "%lu", (unsigned long)sec);
            const int scale = 4;
            const int text_w = static_cast<int>(std::strlen(big)) * Font_6x8.FontWidth * scale;
            const int text_h = Font_6x8.FontHeight * scale;
            const int text_x = (128 - text_w) / 2;
            const int text_y = (64 - text_h) / 2;
            for(int i = 0; big[i] != '\0'; ++i)
                draw_scaled_char(big[i], text_x + i * Font_6x8.FontWidth * scale, text_y, scale);
        }
        break;

        case RecordUiState::Recording:
        {
            d.SetCursor(0, 0);
            d.WriteString("RECORDING - 5 SEC MAX", Font_6x8, true);

            const int wave_y0 = Font_6x8.FontHeight + 2;
            const int wave_y1 = 62;
            const int wave_h = wave_y1 - wave_y0 + 1;
            static float mist_level[128] = {};
            static uint32_t mist_seed = 0xA5B35791u;
            static uint32_t snap_gen = 0;
            static int16_t snap_min[128] = {};
            static int16_t snap_max[128] = {};
            const uint32_t live_gen = app.rec_live_gen.load(std::memory_order_acquire);
            if(live_gen != snap_gen)
            {
                uint32_t g0 = 0, g1 = 0;
                int retry = 0;
                do
                {
                    g0 = app.rec_live_gen.load(std::memory_order_acquire);
                    std::memcpy(snap_min, app.rec_live_min, sizeof(snap_min));
                    std::memcpy(snap_max, app.rec_live_max, sizeof(snap_max));
                    g1 = app.rec_live_gen.load(std::memory_order_acquire);
                } while(g0 != g1 && ++retry < 2);
                snap_gen = g1;
            }

            if(rec_pos == 0)
            {
                for(int x = 0; x < 128; ++x)
                    mist_level[x] = 0.0f;
            }

            const int px = static_cast<int>((static_cast<uint64_t>(rec_pos) * 127u) / kSdSampleMaxFrames);
            const int kTrail = 24; // slightly looser follow
            const float mic_boost = (app.record_source_index == 1) ? 1.5f : 1.0f;
            for(int x = 0; x < 128; ++x)
            {
                int16_t minv = snap_min[x];
                int16_t maxv = snap_max[x];
                int16_t a0 = (minv < 0) ? static_cast<int16_t>(-minv) : minv;
                int16_t a1 = (maxv < 0) ? static_cast<int16_t>(-maxv) : maxv;
                int16_t absmax = (a1 > a0) ? a1 : a0;
                float near = 0.0f;
                const int dist = (x > px) ? (x - px) : (px - x);
                if(dist < kTrail)
                {
                    near = 1.0f - (static_cast<float>(dist) / static_cast<float>(kTrail));
                }
                const float amp = Clamp01((static_cast<float>(absmax) / 32767.0f) * mic_boost);
                const float envelope = 0.26f + (0.74f * near); // loose mist everywhere, strongest near playhead
                const float target = Clamp01(amp * envelope);
                float v = mist_level[x];
                const float rise = 0.97f;
                const float fall = 0.95f;
                v += (target > v) ? ((target - v) * rise) : ((target - v) * fall);
                mist_level[x] = v;
                const float spike_boost = 1.0f + (near * 0.35f); // largest peaks live around playhead on both sides
                int h = static_cast<int>(v * static_cast<float>(wave_h) * 1.35f * spike_boost + 0.5f);
                if(h < 0)
                    h = 0;
                if(h > wave_h)
                    h = wave_h;
                const int start = wave_y1 - h;
                for(int y = start; y <= wave_y1; ++y)
                {
                    mist_seed = mist_seed * 1664525u + 1013904223u;
                    const int frac = wave_y1 - y;
                    const int dens = 1 + (frac / 4);
                    if((mist_seed % static_cast<uint32_t>(dens)) == 0u)
                        d.DrawPixel(x, y, true);
                }
            }

            d.DrawLine(px, wave_y0, px, wave_y1, true);
        }
        break;

        case RecordUiState::Review:
        {
            UiDraw_Header(d, layout, "RECORDED PLAYBACK", status);
            const uint8_t slot = app.record_slot & 1u;
            const Sample& s = app.sd_slots[slot];
            const SampleEdit* e = (s.length > 0) ? &app.sd_edit_slots[slot] : nullptr;
            DrawWaveformPreview(d, s, e, 0, layout.y_body, 128, 50);
            if(s.length == 0)
            {
                d.SetCursor(42, 34);
                d.WriteString("NO AUDIO", Font_6x8, true);
            }
        }
        break;

        case RecordUiState::TargetSelect:
        {
            UiDraw_Header(d, layout, "SAVE SAMPLE?", status);
            const char* a = "SAVE";
            const char* b = "RECORD AGAIN";
            const bool sa = (app.record_target_index == 0);
            const bool sb = !sa;
            d.DrawRect(8, 20, 119, 31, true, sa);
            d.SetCursor(44, 22);
            d.WriteString(a, Font_6x8, !sa);
            d.DrawRect(8, 36, 119, 47, true, sb);
            d.SetCursor(24, 38);
            d.WriteString(b, Font_6x8, !sb);
        }
        break;

        case RecordUiState::BackConfirm:
        {
            UiDraw_Header(d, layout, "ARE YOU SURE?", status);
            d.SetCursor(16, 24);
            d.WriteString("REC WILL BE LOST", Font_6x8, true);
            UiDraw_Footer(d, layout, "R:YES  L:NO");
        }
        break;

        case RecordUiState::SaveWait:
        {
            d.Fill(false);
            if(app.sd.save_in_progress)
            {
                d.SetCursor(0, 0);
                d.WriteString("SAVING", Font_6x8, true);
                const int bar_w = 96;
                const int bar_h = 6;
                const int bar_x = (128 - bar_w) / 2;
                const int bar_y = Font_6x8.FontHeight + 16;
                const int pct = static_cast<int>(app.sd.save_progress);
                const int fill_w = (pct <= 0) ? 0 : ((pct >= 100) ? bar_w : ((bar_w * pct) / 100));
                d.DrawRect(bar_x, bar_y, bar_x + bar_w, bar_y + bar_h, true, false);
                if(fill_w > 0)
                    d.DrawRect(bar_x + 1, bar_y + 1, bar_x + fill_w - 1, bar_y + bar_h - 1, true, true);
            }
            else
            {
                const bool ok = (std::strncmp(app.sd.save_status, "SAVED", 5) == 0);
                d.SetCursor(0, 0);
                d.WriteString(ok ? "SAVE OK" : "SAVE FAILED", Font_6x8, true);
                if(ok && app.sd.save_name[0] != '\0')
                {
                    d.SetCursor(0, Font_6x8.FontHeight + 2);
                    d.WriteString(app.sd.save_name, Font_6x8, true);
                }
            }
        }
        break;
    }

    (void)rec_len;
}

static void Record_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    AppState& app = *ctx.app;
    app.record_state = RecordUiState::SourceSelect;
    app.record_source_index = 0;
    app.record_target_index = 0;
    app.record_slot = app.perform_layer & 1u;
    app.record_anim_start_ms = -1.0;
    app.rec_source_sel.store(app.record_source_index & 1u, std::memory_order_release);
    app.rec_monitor_enable.store(0, std::memory_order_release);
    Record_StopPreview(app);
    app.rec_start_req.store(0, std::memory_order_release);
    app.rec_stop_req.store(0, std::memory_order_release);
    app.ui_dirty = true;
}

static void Record_OnExit(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    AppState& app = *ctx.app;
    Record_StopPreview(app);
    app.rec_monitor_enable.store(0, std::memory_order_release);
    app.record_anim_start_ms = -1.0;
    app.rec_stop_req.store(1, std::memory_order_release);
}

static bool PerformMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        const int32_t next = NextPerformMenuIndex(static_cast<int32_t>(ctx.app->perform_menu_index), e.value);
        ctx.app->perform_menu_index = static_cast<uint8_t>(next);
        ctx.app->ui_dirty = true;
        return true;
    }

    return false;
}

static bool PerformMenu_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return false;

    const uint8_t selected = static_cast<uint8_t>(ctx.app->perform_menu_index % kPerformMenuCount);
    switch(selected)
    {
        case 0:
            return UiNav_Push(ctx.app->ui_nav, UiScreenId::PerformEngine);
        case 1:
            return UiNav_Push(ctx.app->ui_nav, UiScreenId::PerformKeyzone);
        case 2:
            return UiNav_Push(ctx.app->ui_nav, UiScreenId::PerformAdsr);
        case 3:
            return UiNav_Push(ctx.app->ui_nav, UiScreenId::PerformEmphasis);
        case 4:
        default:
            return UiNav_Push(ctx.app->ui_nav, UiScreenId::PerformProcess);
    }
}

static void PerformMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const int selected = static_cast<int>(ctx.app->perform_menu_index % kPerformMenuCount);
    DrawPerformMenuFriendStyle(*ctx.display, selected);
}

static constexpr uint8_t kPerformLayerCount = 2;
static constexpr int32_t kEngineRowCount = 3;
static constexpr int32_t kEngineRowWave = 0;
static constexpr int32_t kEngineRowLoad = 1;
static constexpr int32_t kEngineRowTune = 2;

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static const char* LayerName(uint8_t layer)
{
    return (layer & 1u) ? "B" : "A";
}

static const char* PlayModeName(uint8_t mode)
{
    return (mode == 0) ? "ONESHOT" : "LOOP";
}

static void ExtractBaseName(const char* path, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;
    out[0] = '\0';
    if(!path || path[0] == '\0')
        return;

    const char* base = path;
    for(const char* p = path; *p != '\0'; ++p)
    {
        if(*p == '/' || *p == '\\')
            base = p + 1;
    }
    std::snprintf(out, out_n, "%s", base);
}

static void FormatSignedInt(int v, char* out, size_t out_n)
{
    if(v > 0)
        std::snprintf(out, out_n, "+%d", v);
    else
        std::snprintf(out, out_n, "%d", v);
}

static void FormatDb(int v, char* out, size_t out_n)
{
    if(v > 0)
        std::snprintf(out, out_n, "+%ddB", v);
    else
        std::snprintf(out, out_n, "%ddB", v);
}

static void PublishEngineLayerParams(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.params)
        return;

    const uint8_t layer = ctx.app->perform_layer & 1u;
    PerformParamsTargets& t = ctx.params->EditTargets();
    t.engine_tune_semitones[layer] = static_cast<float>(ctx.app->engine_tune_semitones[layer]);
    t.engine_gain_db[layer] = static_cast<float>(ctx.app->engine_gain_db[layer]);
    t.engine_loop_mode[layer] = (ctx.app->engine_play_mode[layer] != 0);
    ctx.params->PublishTargets();
}

static void EngineRefreshLoadedMetadata(AppState& app)
{
    const uint32_t applied_gen = app.sd_applied_gen.load(std::memory_order_relaxed);
    if(applied_gen == app.engine_seen_applied_gen)
        return;

    app.engine_seen_applied_gen = applied_gen;
    const uint8_t slot = app.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const Sample& s = app.sd_slots[slot];
    if(s.pcm != nullptr && s.length > 0)
    {
        std::snprintf(app.engine_sample_path[slot],
                      sizeof(app.engine_sample_path[slot]),
                      "%s",
                      app.sd.last_loaded_path);
        ExtractBaseName(app.sd.last_loaded_path,
                        app.engine_sample_name[slot],
                        sizeof(app.engine_sample_name[slot]));
    }
    app.engine_load_target_layer = 0xFFu;
    app.engine_load_from_perform = false;
    app.ui_dirty = true;
}

static void DrawWaveformPreview(OledPager& d,
                                const Sample& sample,
                                const SampleEdit* edit,
                                int x,
                                int y,
                                int w,
                                int h)
{
    if(w < 3 || h < 3)
        return;

    const int x0 = x;
    const int y0 = y;
    const int x1 = x + w - 1;
    const int y1 = y + h - 1;
    d.DrawRect(x0, y0, x1, y1, true, false);

    if(sample.pcm == nullptr || sample.length == 0)
        return;

    uint32_t start = 0;
    uint32_t end = sample.length;
    if(edit)
    {
        SampleEdit e = *edit;
        SampleEdit_Clamp(e, sample.length);
        start = e.start_frame;
        end = e.end_frame;
    }
    if(end <= start + 1)
        return;

    const uint32_t frames = end - start;
    const int draw_w = w - 2;
    const int mid = y0 + h / 2;
    const int amp_h = (h - 2) / 2;
    for(int px = 0; px < draw_w; ++px)
    {
        const uint32_t seg0 = start + (frames * static_cast<uint32_t>(px)) / draw_w;
        uint32_t seg1 = start + (frames * static_cast<uint32_t>(px + 1)) / draw_w;
        if(seg1 <= seg0)
            seg1 = seg0 + 1;
        if(seg1 > end)
            seg1 = end;

        int16_t mn = 32767;
        int16_t mx = -32768;
        for(uint32_t i = seg0; i < seg1; ++i)
        {
            const int16_t v = sample.pcm[i];
            if(v < mn)
                mn = v;
            if(v > mx)
                mx = v;
        }
        const int y_top = mid - (static_cast<int>(mx) * amp_h) / 32768;
        const int y_bot = mid - (static_cast<int>(mn) * amp_h) / 32768;
        const int xx = x0 + 1 + px;
        int top = y_top;
        int bot = y_bot;
        if(top < y0 + 1) top = y0 + 1;
        if(bot > y1 - 1) bot = y1 - 1;
        if(bot < top)
            bot = top;
        for(int yy = top; yy <= bot; ++yy)
            d.DrawPixel(xx, yy, true);
    }
}

static void PerformEngine_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;

    EngineRefreshLoadedMetadata(*ctx.app);
    const bool pending_engine_load = ctx.app->engine_load_from_perform
                                     && (ctx.app->engine_load_target_layer < kPerformLayerCount);
    const uint8_t layer = ctx.app->perform_layer & 1u;
    if(!pending_engine_load)
        ctx.app->sd_current_slot.store(layer, std::memory_order_release);

    ctx.app->engine_load_from_perform = false;
    ctx.app->engine_load_target_layer = 0xFFu;
    PublishEngineLayerParams(ctx);
    ctx.app->ui_dirty = true;
}

static bool PerformEngine_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return false;

    const uint8_t row = ctx.app->perform_engine_row % static_cast<uint8_t>(kEngineRowCount);
    if(row == kEngineRowWave)
        return UiNav_Push(ctx.app->ui_nav, UiScreenId::PerformWaveEdit);
    if(row != kEngineRowLoad)
        return false;

    ctx.app->engine_load_target_layer = ctx.app->perform_layer & 1u;
    ctx.app->engine_load_from_perform = true;
    return UiNav_Push(ctx.app->ui_nav, UiScreenId::SdBrowse);
}

static bool PerformEngine_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    if(ctx.shift)
        return false;

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        app.perform_layer ^= 1u;
        const uint8_t layer = app.perform_layer & 1u;
        app.sd_current_slot.store(layer, std::memory_order_release);
        PublishEngineLayerParams(ctx);
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        int row = static_cast<int>(app.perform_engine_row);
        row += e.value;
        while(row < 0)
            row += kEngineRowCount;
        while(row >= kEngineRowCount)
            row -= kEngineRowCount;
        app.perform_engine_row = static_cast<uint8_t>(row);
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const uint8_t layer = app.perform_layer & 1u;
        const uint8_t row = app.perform_engine_row % static_cast<uint8_t>(kEngineRowCount);
        bool changed = false;
        if(row == kEngineRowTune)
        {
            int v = static_cast<int>(app.engine_tune_semitones[layer]) + e.value;
            v = ClampInt(v, -24, 24);
            const int8_t vv = static_cast<int8_t>(v);
            if(vv != app.engine_tune_semitones[layer])
            {
                app.engine_tune_semitones[layer] = vv;
                changed = true;
            }
        }

        if(changed)
        {
            PublishEngineLayerParams(ctx);
            app.ui_dirty = true;
            return true;
        }
    }

    return false;
}

static void PerformEngine_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);

    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t layer = app.perform_layer & 1u;
    const Sample& sample = app.sd_slots[layer];
    const bool sample_loaded = (sample.pcm != nullptr && sample.length > 0);
    const SampleEdit* edit = sample_loaded ? &app.sd_edit_slots[layer] : nullptr;

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    char title[16];
    std::snprintf(title, sizeof(title), "ENGINE %s", LayerName(layer));
    UiDraw_Header(d, layout, title, status);

    const char* name = sample_loaded ? app.engine_sample_name[layer] : "NO SAMPLE LOADED";
    if(name == nullptr || name[0] == '\0')
        name = sample_loaded ? "LOADED" : "NO SAMPLE LOADED";
    char name_buf[24];
    std::snprintf(name_buf, sizeof(name_buf),name);
    d.SetCursor(layout.x, layout.y_body);
    d.WriteString(name_buf, Font_6x8, true);

    constexpr int kWaveX = 0;
    const int kWaveY = layout.y_body + layout.line_h;
    constexpr int kWaveW = 128;
    const int kLoadY = layout.y_footer - layout.line_h;
    const int kTuneY = layout.y_footer;
    const int kWaveH = kLoadY - kWaveY;
    DrawWaveformPreview(d, sample, edit, kWaveX, kWaveY, kWaveW, kWaveH);
    const uint8_t row = app.perform_engine_row % static_cast<uint8_t>(kEngineRowCount);
    if(row == kEngineRowWave)
    {
        // Highlight full waveform region when waveform row is selected.
        d.DrawRect(kWaveX, kWaveY, kWaveX + kWaveW - 1, kWaveY + kWaveH - 1, true, false);
        d.DrawRect(kWaveX + 1, kWaveY + 1, kWaveX + kWaveW - 2, kWaveY + kWaveH - 2, true, false);
    }

    char tune_buf[12];
    FormatSignedInt(app.engine_tune_semitones[layer], tune_buf, sizeof(tune_buf));

    char line[32];

    std::snprintf(line, sizeof(line), "%c LOAD", (row == kEngineRowLoad) ? '>' : ' ');
    d.SetCursor(0, kLoadY);
    d.WriteString(line, Font_6x8, true);

    std::snprintf(line, sizeof(line), "%c TUNE:%s", (row == kEngineRowTune) ? '>' : ' ', tune_buf);
    d.SetCursor(0, kTuneY);
    d.WriteString(line, Font_6x8, true);
}

static void PerformWaveEdit_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const UiLayout layout = UiLayout_Default();
    OledPager& d = *ctx.display;
    d.Fill(false);

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);
    const uint8_t layer = app.perform_layer & 1u;
    app.sd_current_slot.store(layer, std::memory_order_release);
    const Sample& sample = app.sd_slots[layer];
    const bool sample_loaded = (sample.pcm != nullptr && sample.length > 0);
    SampleEdit edit = app.sd_edit_slots[layer];
    SampleEdit_Clamp(edit, sample.length);

    char status[16];
    BuildStatus(app, status, sizeof(status));

    char title[24];
    std::snprintf(title, sizeof(title), ".WAV EDIT %s TRIM", LayerName(layer));
    UiDraw_Header(d, layout, title, status);

    const char* name = sample_loaded ? app.engine_sample_name[layer] : "NO SAMPLE LOADED";
    if(name == nullptr || name[0] == '\0')
        name = sample_loaded ? "LOADED" : "NO SAMPLE LOADED";
    char name_buf[24];
    std::snprintf(name_buf, sizeof(name_buf), "%s", name);
    d.SetCursor(layout.x, layout.y_body);
    d.WriteString(name_buf, Font_6x8, true);

    const int wave_x = 0;
    const int wave_y = layout.y_body + layout.line_h;
    const int wave_w = 128;
    const int wave_h = layout.y_footer - wave_y;
    const int x0 = wave_x;
    const int y0 = wave_y;
    const int x1 = wave_x + wave_w - 1;
    const int y1 = wave_y + wave_h - 1;
    d.DrawRect(x0, y0, x1, y1, true, false);

    if(sample_loaded && wave_w >= 3 && wave_h >= 3)
    {
        const uint32_t frames = sample.length;
        const uint32_t denom = (frames > 1) ? (frames - 1) : 1;
        int start_x = x0 + static_cast<int>((static_cast<uint64_t>(edit.start_frame) * (wave_w - 1)) / denom);
        int end_x = x0 + static_cast<int>((static_cast<uint64_t>(edit.end_frame) * (wave_w - 1)) / denom);
        if(end_x < start_x)
        {
            const int t = start_x;
            start_x = end_x;
            end_x = t;
        }
        if(start_x < x0) start_x = x0;
        if(end_x > x1) end_x = x1;

        const int mid = y0 + wave_h / 2;
        const int amp_h = (wave_h - 2) / 2;
        const int draw_w = wave_w - 2;
        const uint32_t total = sample.length;
        for(int px = 0; px < draw_w; ++px)
        {
            const uint32_t seg0 = (static_cast<uint64_t>(total) * static_cast<uint32_t>(px)) / draw_w;
            uint32_t seg1 = (static_cast<uint64_t>(total) * static_cast<uint32_t>(px + 1)) / draw_w;
            if(seg1 <= seg0)
                seg1 = seg0 + 1;
            if(seg1 > total)
                seg1 = total;

            int16_t mn = 32767;
            int16_t mx = -32768;
            for(uint32_t i = seg0; i < seg1; ++i)
            {
                const int16_t v = sample.pcm[i];
                if(v < mn)
                    mn = v;
                if(v > mx)
                    mx = v;
            }

            int top = mid - (static_cast<int>(mx) * amp_h) / 32768;
            int bot = mid - (static_cast<int>(mn) * amp_h) / 32768;
            if(top < y0 + 1) top = y0 + 1;
            if(bot > y1 - 1) bot = y1 - 1;
            if(bot < top) bot = top;

            const int xx = x0 + 1 + px;
            const bool inside = (xx >= start_x && xx <= end_x);
            if(inside)
            {
                for(int yy = top; yy <= bot; ++yy)
                {
                    if((yy & 1) == 0)
                        d.DrawPixel(xx, yy, true);
                }
            }
            else
            {
                d.DrawLine(xx, top, xx, bot, true);
            }
        }

        auto draw_bracket = [&](int hx, bool start_handle)
        {
            int x = hx;
            if(x < x0) x = x0;
            if(x > x1) x = x1;
            for(int yy = y0; yy <= y1; ++yy)
            {
                d.DrawPixel(x, yy, true);
                if(x + 1 <= x1)
                    d.DrawPixel(x + 1, yy, true);
            }

            const int cap = 5;
            for(int dx = 0; dx < cap; ++dx)
            {
                const int px = start_handle ? (x + dx) : (x - dx);
                if(px >= x0 && px <= x1)
                {
                    d.DrawPixel(px, y0, true);
                    d.DrawPixel(px, y1, true);
                }
            }
        };

        draw_bracket(start_x, true);
        draw_bracket(end_x, false);

        const uint32_t ph_active = app.playhead_active[layer].load(std::memory_order_relaxed);
        if(ph_active != 0u)
        {
            const uint32_t ph_frame = app.playhead_frame[layer].load(std::memory_order_relaxed);
            const uint32_t ph = (ph_frame >= frames) ? (frames - 1) : ph_frame;
            const int play_x = x0 + static_cast<int>((static_cast<uint64_t>(ph) * (wave_w - 1)) / denom);
            d.DrawLine(play_x, y0, play_x, y1, true);
        }
    }

}

static bool PerformWaveEdit_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    AppState& app = *ctx.app;
    const uint8_t layer = app.perform_layer & 1u;
    app.sd_current_slot.store(layer, std::memory_order_release);
    Sample& sample = app.sd_slots[layer];
    if(sample.pcm == nullptr || sample.length == 0)
        return false;

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        app.perform_layer ^= 1u;
        const uint8_t next = app.perform_layer & 1u;
        app.sd_current_slot.store(next, std::memory_order_release);
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta
       && (e.id == kUiEncPod || e.id == kUiEncExt)
       && e.value != 0)
    {
        SampleEdit edit = app.sd_edit_slots[layer];
        SampleEdit_Clamp(edit, sample.length);

        const uint32_t frames = sample.length;
        if(frames < 2u)
            return false;
        const float denom = static_cast<float>(frames);
        float trim_start = static_cast<float>(edit.start_frame) / denom;
        float trim_end = static_cast<float>(edit.end_frame) / denom;

        const int32_t start_delta = (e.id == kUiEncPod) ? e.value : 0;
        const int32_t end_delta = (e.id == kUiEncExt) ? e.value : 0;
        const float base_step = ctx.rshift ? (1.0f / 64.0f) : (1.0f / 32.0f);
        auto step = [&](int d)
        {
            int mag = (d < 0) ? -d : d;
            if(mag < 1)
                mag = 1;
            int log2 = 0;
            while(mag > 1)
            {
                mag >>= 1;
                ++log2;
            }
            return base_step * static_cast<float>(1 << log2);
        };

        if(start_delta != 0)
            trim_start += static_cast<float>(start_delta) * step(start_delta);
        if(end_delta != 0)
            trim_end += static_cast<float>(end_delta) * step(end_delta);

        if(trim_start < 0.0f)
            trim_start = 0.0f;
        if(trim_end > 1.0f)
            trim_end = 1.0f;
        const float min_norm = 2.0f / static_cast<float>(frames);
        if((trim_end - trim_start) < min_norm)
        {
            trim_end = trim_start + min_norm;
            if(trim_end > 1.0f)
            {
                trim_end = 1.0f;
                trim_start = trim_end - min_norm;
            }
        }

        uint32_t start_frame = static_cast<uint32_t>(trim_start * static_cast<float>(frames));
        uint32_t end_frame = static_cast<uint32_t>(trim_end * static_cast<float>(frames));
        if(end_frame <= start_frame)
            end_frame = start_frame + 2u;
        if(end_frame > frames)
            end_frame = frames;
        if(start_frame >= end_frame)
            start_frame = (end_frame > 0u) ? (end_frame - 1u) : 0u;
        edit.start_frame = start_frame;
        edit.end_frame = end_frame;

        SampleEdit_Clamp(edit, frames);
        app.sd_edit_slots[layer] = edit;
        app.sd_edit_pending = edit;
        app.sd_edit_slot.store(layer, std::memory_order_release);
        app.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
        app.sd_edit_ready.store(1, std::memory_order_release);
        app.ui_dirty = true;
        return true;
    }

    return false;
}

static bool PerformKeyzone_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    if(ctx.shift)
        return false;

    AppState& app = *ctx.app;

    // POD2 toggles layer (same behavior as ENGINE).
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        app.perform_layer ^= 1u;
        const uint8_t layer = app.perform_layer & 1u;
        app.sd_current_slot.store(layer, std::memory_order_release);
        PublishEngineLayerParams(ctx);
        app.ui_dirty = true;
        return true;
    }
    return false;
}

static bool PerformAdsr_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    if(ctx.shift)
        return false;

    AppState& app = *ctx.app;

    // R encoder toggles MODE (ONESHOT/LOOP) for the active layer.
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const uint8_t layer = app.perform_layer & 1u;

        int mode = static_cast<int>(app.engine_play_mode[layer]) & 1;
        int steps = e.value;
        if(steps < 0)
            steps = -steps;
        if((steps & 1) != 0)
            mode ^= 1;

        const uint8_t next_mode = static_cast<uint8_t>(mode);
        if(next_mode != app.engine_play_mode[layer])
        {
            app.engine_play_mode[layer] = next_mode;
            PublishEngineLayerParams(ctx);
            app.ui_dirty = true;
        }
        return true;
    }

    // POD2 toggles layer (same behavior as ENGINE).
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        app.perform_layer ^= 1u;
        const uint8_t layer = app.perform_layer & 1u;
        app.sd_current_slot.store(layer, std::memory_order_release);
        PublishEngineLayerParams(ctx);
        app.ui_dirty = true;
        return true;
    }

    return false;
}

static bool PerformEmphasis_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    if(ctx.shift)
        return false;

    AppState& app = *ctx.app;

    // R encoder adjusts GAIN (dB) for the active layer.
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const uint8_t layer = app.perform_layer & 1u;

        int v = static_cast<int>(app.engine_gain_db[layer]) + e.value;
        v = ClampInt(v, -32, 6);
        const int8_t vv = static_cast<int8_t>(v);
        if(vv != app.engine_gain_db[layer])
        {
            app.engine_gain_db[layer] = vv;
            PublishEngineLayerParams(ctx);
            app.ui_dirty = true;
        }
        return true;
    }

    // POD2 toggles layer (same behavior as ENGINE).
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        app.perform_layer ^= 1u;
        const uint8_t layer = app.perform_layer & 1u;
        app.sd_current_slot.store(layer, std::memory_order_release);
        PublishEngineLayerParams(ctx);
        app.ui_dirty = true;
        return true;
    }

    return false;
}

static void PerformKeyzone_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);

    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t layer = app.perform_layer & 1u;
    const Sample& sample = app.sd_slots[layer];
    const bool sample_loaded = (sample.pcm != nullptr && sample.length > 0);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    char title[16];
    std::snprintf(title, sizeof(title), "KEYZONE %s", LayerName(layer));
    UiDraw_Header(d, layout, title, status);

    const char* name = sample_loaded ? app.engine_sample_name[layer] : "NO SAMPLE LOADED";
    if(name == nullptr || name[0] == '\0')
        name = sample_loaded ? "LOADED" : "NO SAMPLE LOADED";

    // WAV file name under header (no footer hints)
    d.SetCursor(layout.x, layout.y_body);
    d.WriteString(name, Font_6x8, true);
}

static void PerformAdsr_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);

    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t layer = app.perform_layer & 1u;
    const Sample& sample = app.sd_slots[layer];
    const bool sample_loaded = (sample.pcm != nullptr && sample.length > 0);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    char title[16];
    std::snprintf(title, sizeof(title), "ADSR %s", LayerName(layer));
    UiDraw_Header(d, layout, title, status);

    const char* name = sample_loaded ? app.engine_sample_name[layer] : "NO SAMPLE LOADED";
    if(name == nullptr || name[0] == '\0')
        name = sample_loaded ? "LOADED" : "NO SAMPLE LOADED";

    // WAV file name under header
    d.SetCursor(layout.x, layout.y_body);
    d.WriteString(name, Font_6x8, true);

    // MODE row (footer hints removed)
    char line[32];
    std::snprintf(line, sizeof(line), "> MODE:%s", PlayModeName(app.engine_play_mode[layer]));
    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    d.WriteString(line, Font_6x8, true);
}

static void PerformEmphasis_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);

    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t layer = app.perform_layer & 1u;
    const Sample& sample = app.sd_slots[layer];
    const bool sample_loaded = (sample.pcm != nullptr && sample.length > 0);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    char title[16];
    std::snprintf(title, sizeof(title), "EMPH %s", LayerName(layer));
    UiDraw_Header(d, layout, title, status);

    const char* name = sample_loaded ? app.engine_sample_name[layer] : "NO SAMPLE LOADED";
    if(name == nullptr || name[0] == '\0')
        name = sample_loaded ? "LOADED" : "NO SAMPLE LOADED";

    // WAV file name under header
    d.SetCursor(layout.x, layout.y_body);
    d.WriteString(name, Font_6x8, true);

    // GAIN row (footer hints removed)
    char gain_buf[12];
    FormatDb(app.engine_gain_db[layer], gain_buf, sizeof(gain_buf));

    char line[32];
    std::snprintf(line, sizeof(line), "> GAIN:%s", gain_buf);
    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    d.WriteString(line, Font_6x8, true);
}

static bool PerformProcess_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app || !ctx.params)
        return false;
    if(ctx.shift)
        return false;

    AppState& app = *ctx.app;
    const uint8_t main_cursor = static_cast<uint8_t>(app.perform_process_main_cursor % 6u);
    const bool main_selects_fx = (main_cursor >= 2u);
    const uint8_t cursor = main_selects_fx ? static_cast<uint8_t>((main_cursor - 2u) & 0x03u)
                                           : static_cast<uint8_t>(app.perform_process_fx_cursor & 0x03u);
    const uint8_t fx_id = app.perform_process_fx_order[cursor];

    auto detail_param_count = [](uint8_t id) -> uint8_t
    {
        switch(id)
        {
            case 0: return 4; // SAT + mode toggle
            case 1: return 4; // MOD + algo toggle
            case 2: return 5; // DELAY + FRZ toggle
            case 3: return 5; // REVERB + DIR toggle
            default: return 3;
        }
    };

    // POD2 toggles layer (same behavior as other PERFORM pages).
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        app.perform_layer ^= 1u;
        const uint8_t layer = app.perform_layer & 1u;
        app.sd_current_slot.store(layer, std::memory_order_release);
        PublishEngineLayerParams(ctx);
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown
       && (e.id == kUiBtnPod1 || e.id == kUiBtnPodEnc)
       && app.perform_process_detail_active)
    {
        app.perform_process_detail_active = false;
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(!app.perform_process_detail_active)
        {
            if(!main_selects_fx)
            {
                app.ui_dirty = true;
                return true;
            }
            app.perform_process_detail_active = true;
            app.ui_dirty = true;
            return true;
        }

        // In ADSR-style FX detail, toggles change via encoder scroll, not click.
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        if(app.perform_process_detail_active)
        {
            int idx = static_cast<int>(app.perform_process_detail_param[cursor]) + e.value;
            const int count = static_cast<int>(detail_param_count(fx_id));
            while(idx < 0)
                idx += count;
            while(idx >= count)
                idx -= count;
            app.perform_process_detail_param[cursor] = static_cast<uint8_t>(idx);
            app.ui_dirty = true;
            return true;
        }
        int idx = static_cast<int>(main_cursor) + e.value;
        while(idx < 0)
            idx += 6;
        while(idx >= 6)
            idx -= 6;
        app.perform_process_main_cursor = static_cast<uint8_t>(idx);
        if(app.perform_process_main_cursor >= 2u)
            app.perform_process_fx_cursor = static_cast<uint8_t>((app.perform_process_main_cursor - 2u) & 0x03u);
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        if(app.perform_process_detail_active)
        {
            PerformParamsTargets& t = ctx.params->EditTargets();
            const uint8_t pidx = app.perform_process_detail_param[cursor];
            const float step = 0.02f;
            const float delta = static_cast<float>(e.value) * step;
            bool changed = false;
            switch(fx_id)
            {
                case 0: // SAT
                    if(pidx == 0)
                    {
                        if(t.sat_mode == 0)
                        {
                            t.sat_drive = Clamp01(t.sat_drive + delta);
                        }
                        else
                        {
                            // ADSR behavior: RESO is a discrete 3-state selector.
                            const int dir = (e.value > 0) ? 1 : -1;
                            int idx = 0;
                            if(t.sat_bit_reso >= 0.75f)
                                idx = 2;
                            else if(t.sat_bit_reso >= 0.25f)
                                idx = 1;
                            idx += dir;
                            if(idx < 0) idx = 0;
                            if(idx > 2) idx = 2;
                            if(idx == 0) t.sat_bit_reso = 0.0f;      // CRUSH
                            if(idx == 1) t.sat_bit_reso = 0.5f;      // STATIC
                            if(idx == 2) t.sat_bit_reso = 1.0f;      // HISS
                        }
                        changed = true;
                    }
                    else if(pidx == 1)
                    {
                        if(t.sat_mode == 0)
                            t.sat_bump = Clamp01(t.sat_bump + delta);
                        else
                            t.sat_bit_smpl = Clamp01(t.sat_bit_smpl + delta);
                        changed = true;
                    }
                    else if(pidx == 2)
                    {
                        t.sat_mix = Clamp01(t.sat_mix + delta);
                        t.sat_on = (t.sat_mix > 0.001f);
                        changed = true;
                    }
                    else if(pidx == 3)
                    {
                        const int dir = (e.value > 0) ? 1 : -1;
                        int steps = (e.value > 0) ? e.value : -e.value;
                        while(steps-- > 0)
                            t.sat_mode = (dir > 0) ? ((t.sat_mode + 1u) & 1u)
                                                   : ((t.sat_mode == 0) ? 1u : 0u);
                        changed = true;
                    }
                    break;
                case 1: // MOD
                    if(pidx == 0)
                    {
                        if(t.mod_mode == 1)
                            t.mod_wow = Clamp01(t.mod_wow + delta);
                        else
                            t.lfo_depth = Clamp01(t.lfo_depth + delta);
                        changed = true;
                    }
                    else if(pidx == 1)
                    {
                        if(t.mod_mode == 1)
                            t.tape_rate = Clamp01(t.tape_rate + delta);
                        else
                            t.mod_rate_hz = Clamp01(t.mod_rate_hz + delta);
                        changed = true;
                    }
                    else if(pidx == 2)
                    {
                        t.mod_mix = Clamp01(t.mod_mix + delta);
                        t.mod_on = (t.mod_mix > 0.001f);
                        changed = true;
                    }
                    else if(pidx == 3)
                    {
                        const int dir = (e.value > 0) ? 1 : -1;
                        int steps = (e.value > 0) ? e.value : -e.value;
                        while(steps-- > 0)
                            t.mod_mode = (dir > 0) ? ((t.mod_mode + 1u) & 1u)
                                                   : ((t.mod_mode == 0) ? 1u : 0u);
                        changed = true;
                    }
                    break;
                case 2: // DELAY
                    if(pidx == 0)
                    {
                        t.delay_time = Clamp01(t.delay_time + delta);
                        changed = true;
                    }
                    else if(pidx == 1)
                    {
                        t.delay_feedback = Clamp01(t.delay_feedback + delta);
                        changed = true;
                    }
                    else if(pidx == 2)
                    {
                        t.delay_spread = Clamp01(t.delay_spread + delta);
                        changed = true;
                    }
                    else if(pidx == 4)
                    {
                        t.delay_mix = Clamp01(t.delay_mix + delta);
                        t.delay_on = (t.delay_mix > 0.001f);
                        changed = true;
                    }
                    else if(pidx == 3)
                    {
                        int steps = (e.value > 0) ? e.value : -e.value;
                        while(steps-- > 0)
                            t.delay_freeze = (t.delay_freeze >= 0.5f) ? 0.0f : 1.0f;
                        changed = true;
                    }
                    break;
                case 3: // REVERB
                default:
                    if(pidx == 0)
                    {
                        t.reverb_pre = Clamp01(t.reverb_pre + delta);
                        changed = true;
                    }
                    else if(pidx == 1)
                    {
                        t.reverb_damp = Clamp01(t.reverb_damp + delta);
                        changed = true;
                    }
                    else if(pidx == 2)
                    {
                        t.reverb_decay = Clamp01(t.reverb_decay + delta);
                        changed = true;
                    }
                    else if(pidx == 4)
                    {
                        t.reverb_mix = Clamp01(t.reverb_mix + delta);
                        t.reverb_on = (t.reverb_mix > 0.001f);
                        changed = true;
                    }
                    else if(pidx == 3)
                    {
                        int steps = (e.value > 0) ? e.value : -e.value;
                        while(steps-- > 0)
                            t.reverb_reverse = !t.reverb_reverse;
                        changed = true;
                    }
                    break;
            }
            if(changed)
                ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }

        if(ctx.rshift)
        {
            if(!main_selects_fx)
            {
                app.ui_dirty = true;
                return true;
            }
            const int dir = (e.value > 0) ? 1 : -1;
            int steps = (e.value > 0) ? e.value : -e.value;
            while(steps-- > 0)
            {
                const int from = static_cast<int>(app.perform_process_fx_cursor & 0x03u);
                int to = from + dir;
                if(to < 0)
                    to = 3;
                else if(to > 3)
                    to = 0;
                const uint8_t tmp = app.perform_process_fx_order[from];
                app.perform_process_fx_order[from] = app.perform_process_fx_order[to];
                app.perform_process_fx_order[to] = tmp;
                app.perform_process_fx_cursor = static_cast<uint8_t>(to);
            }

            PerformParamsTargets& t = ctx.params->EditTargets();
            for(int i = 0; i < 4; ++i)
                t.fx_order[i] = app.perform_process_fx_order[i];
            ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }

        if(!main_selects_fx)
        {
            app.ui_dirty = true;
            return true;
        }

        PerformParamsTargets& t = ctx.params->EditTargets();
        const float step = 0.02f;
        const float delta = static_cast<float>(e.value) * step;

        switch(fx_id)
        {
            case 0: // S = saturation drive
                t.sat_drive = Clamp01(t.sat_drive + delta);
                t.sat_on = (t.sat_drive > 0.001f);
                break;
            case 1: // M = modulation amount (existing plumbing)
                t.lfo_depth = Clamp01(t.lfo_depth + delta);
                break;
            case 2: // D = delay wet
                t.delay_mix = Clamp01(t.delay_mix + delta);
                t.delay_on = (t.delay_mix > 0.001f);
                break;
            case 3: // R = reverb wet
            default:
                t.reverb_mix = Clamp01(t.reverb_mix + delta);
                t.reverb_on = (t.reverb_mix > 0.001f);
                break;
        }
        for(int i = 0; i < 4; ++i)
            t.fx_order[i] = app.perform_process_fx_order[i];

        ctx.params->PublishTargets();
        app.ui_dirty = true;
        return true;
    }

    return false;
}

static void DrawFxDetailScreen(OledPager& d,
                               const PerformParamsTargets& t,
                               uint8_t fx_id,
                               uint8_t selected_param,
                               uint32_t now_ms)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;
    constexpr int kPerformFaderCount = 4;
    constexpr int kDelayFaderCount = 5;
    constexpr int kReverbFaderCount = 5;
    constexpr int kBitResoStepCount = 3;
    static const char* kBitResoLabels[kBitResoStepCount] = {"CRUSH", "STATIC", "HISS"};
    auto bit_reso_index = [](float value) -> int
    {
        if(value < 0.0f) value = 0.0f;
        if(value > 1.0f) value = 1.0f;
        int idx = static_cast<int>(value * static_cast<float>(kBitResoStepCount - 1) + 0.5f);
        if(idx < 0) idx = 0;
        if(idx >= kBitResoStepCount) idx = kBitResoStepCount - 1;
        return idx;
    };

    const char* labels[kPerformFaderCount] = {"SATURATION", "MODULATION", "DELAY", "REVERB"};
    int index = static_cast<int>(fx_id & 0x03u);
    if(index < 0 || index >= kPerformFaderCount)
        index = 0;

    const char* label = labels[index];
    const int text_w = TinyStringWidth(label);
    int text_x = (kDisplayW - text_w) / 2;
    if(text_x < 0)
        text_x = 0;
    DrawTinyString(d, label, text_x, 1, true);

    if(index == 0)
    {
        constexpr int kMargin = 2;
        constexpr int kGap = 2;
        const int block_x = kMargin;
        const int block_w = kDisplayW / 4;
        const int block_y = Font5x7::H + 4;
        int block_h = kDisplayH - block_y - kMargin;
        if(block_h < 3) block_h = 3;
        const int box_h = (block_h - kGap) / 2;
        const bool tape_selected = (t.sat_mode == 0);
        const bool bit_selected = (t.sat_mode == 1);
        const bool mode_select_active = (selected_param == 3);
        if(mode_select_active)
            d.DrawRect(block_x - 1, block_y - 1, block_x + block_w, block_y + block_h, true, false);
        d.DrawRect(block_x, block_y, block_x + block_w - 1, block_y + box_h - 1, true, tape_selected);
        d.DrawRect(block_x, block_y + box_h + kGap, block_x + block_w - 1, block_y + (box_h * 2) + kGap - 1, true, bit_selected);
        const int label_w1 = TinyStringWidth("TAPE");
        const int label_w2 = TinyStringWidth("BIT");
        const int label_y1 = block_y + (box_h - Font5x7::H) / 2;
        const int label_y2 = block_y + box_h + kGap + (box_h - Font5x7::H) / 2;
        int label_x1 = block_x + (block_w - label_w1) / 2;
        int label_x2 = block_x + (block_w - label_w2) / 2;
        if(label_x1 < block_x + 1) label_x1 = block_x + 1;
        if(label_x2 < block_x + 1) label_x2 = block_x + 1;
        DrawTinyString(d, "TAPE", label_x1, label_y1, !tape_selected);
        DrawTinyString(d, "BIT", label_x2, label_y2, !bit_selected);

        const int fader_offset = 8;
        const int fader_x = block_x + block_w + kGap + fader_offset;
        const int fader_w = kDisplayW - fader_x - kMargin;
        if(fader_w > 4)
        {
            const char* fader_labels[3] = {(t.sat_mode == 1) ? "RESO" : "SAT",
                                           (t.sat_mode == 1) ? "SMPL" : "BUMP",
                                           "MIX"};
            const float fader_values[3] = {(t.sat_mode == 1) ? t.sat_bit_reso : t.sat_drive,
                                           (t.sat_mode == 1) ? t.sat_bit_smpl : t.sat_bump,
                                           t.sat_mix};
            int param_index = selected_param;
            const bool fader_select_active = (param_index >= 0 && param_index < 3);
            if(!fader_select_active && !mode_select_active) param_index = 0;
            const int fader_offsets[3] = {0, 0, 0};
            const bool circle_handles[3] = {false, false, false};
            const bool hide_rails[3] = {t.sat_mode == 1, false, false};
            const bool hide_handles[3] = {t.sat_mode == 1, false, false};
            DrawVerticalFadersInRect(d, fader_x, block_y, fader_w, block_h,
                                     fader_labels, fader_values, 3, fader_select_active, param_index,
                                     fader_offsets, circle_handles, hide_rails, hide_handles);
            if(t.sat_mode == 1)
            {
                const int label_y = block_y + block_h - Font5x7::H - 1;
                int line_top = block_y + 2;
                int line_bottom = label_y - 2;
                if(line_bottom > line_top)
                {
                    int fader_left = fader_x + 2;
                    int line_x = fader_left;
                    const char* lbl = "RESO";
                    const int lbl_w = TinyStringWidth(lbl);
                    int lbl_x = line_x - (lbl_w / 2);
                    if(lbl_x < fader_x + 1) lbl_x = fader_x + 1;
                    if(lbl_x + lbl_w > fader_x + fader_w - 2) lbl_x = fader_x + fader_w - 2 - lbl_w;
                    line_x = lbl_x + (lbl_w / 2);
                    const int cur_idx = bit_reso_index(t.sat_bit_reso);
                    const int label_top = line_top + 1;
                    const int label_gap = 3;
                    int label_y0 = label_top;
                    for(int i = 0; i < kBitResoStepCount; ++i)
                    {
                        const char* bits_label = kBitResoLabels[i];
                        const int bits_w = TinyStringWidth(bits_label);
                        const int bits_x = line_x - (bits_w / 2);
                        const int bits_y = label_y0 + (i * (Font5x7::H + label_gap));
                        if(bits_y >= line_top && bits_y <= line_bottom - Font5x7::H)
                        {
                            const bool is_selected = (i == cur_idx);
                            if(is_selected)
                            {
                                d.DrawRect(bits_x - 1, bits_y - 1, bits_x + bits_w, bits_y + Font5x7::H, true, true);
                                DrawTinyString(d, bits_label, bits_x, bits_y, false);
                            }
                            else
                            {
                                DrawTinyString(d, bits_label, bits_x, bits_y, true);
                            }
                        }
                    }
                }
            }
        }
    }
    else if(index == 1)
    {
        constexpr int kMargin = 2;
        constexpr int kGap = 2;
        const int block_x = kMargin;
        const int block_w = kDisplayW / 4;
        const int block_y = Font5x7::H + 4;
        int block_h = kDisplayH - block_y - kMargin;
        if(block_h < 3) block_h = 3;
        const bool chorus_selected = (t.mod_mode == 0);
        const bool tape_selected = (t.mod_mode == 1);
        const bool algo_selected = (selected_param == 3);
        const int algo_x0 = block_x;
        const int algo_y0 = block_y;
        const int algo_x1 = block_x + block_w - 1;
        const int algo_y1 = block_y + block_h - 1;
        const int algo_pad = 2;
        const int algo_gap = 2;
        const int inner_x0 = algo_x0 + algo_pad;
        const int inner_y0 = algo_y0 + algo_pad;
        const int inner_x1 = algo_x1 - algo_pad;
        const int inner_y1 = algo_y1 - algo_pad;
        const int inner_h = inner_y1 - inner_y0 + 1;
        const int box_h = (inner_h - algo_gap) / 2;
        const int box1_y0 = inner_y0;
        const int box1_y1 = box1_y0 + box_h - 1;
        const int box2_y0 = box1_y1 + algo_gap + 1;
        const int box2_y1 = box2_y0 + box_h - 1;
        if(algo_selected)
        {
            d.DrawRect(algo_x0, algo_y0, algo_x1, algo_y1, true, false);
            if(algo_x1 - algo_x0 > 2 && algo_y1 - algo_y0 > 2)
                d.DrawRect(algo_x0 + 1, algo_y0 + 1, algo_x1 - 1, algo_y1 - 1, true, false);
        }
        d.DrawRect(inner_x0, box1_y0, inner_x1, box1_y1, true, chorus_selected);
        d.DrawRect(inner_x0, box2_y0, inner_x1, box2_y1, true, tape_selected);
        const int label_w1 = TinyStringWidth("CHRS");
        const int label_w2 = TinyStringWidth("TAPE");
        const int label_y1 = box1_y0 + (box_h - Font5x7::H) / 2;
        const int label_y2 = box2_y0 + (box_h - Font5x7::H) / 2;
        const int inner_w = inner_x1 - inner_x0 + 1;
        int label_x1 = inner_x0 + (inner_w - label_w1) / 2;
        int label_x2 = inner_x0 + (inner_w - label_w2) / 2;
        if(label_x1 < inner_x0 + 1) label_x1 = inner_x0 + 1;
        if(label_x1 + label_w1 > inner_x1 - 1) label_x1 = inner_x1 - 1 - label_w1;
        if(label_x2 < inner_x0 + 1) label_x2 = inner_x0 + 1;
        if(label_x2 + label_w2 > inner_x1 - 1) label_x2 = inner_x1 - 1 - label_w2;
        DrawTinyString(d, "CHRS", label_x1, label_y1, !chorus_selected);
        DrawTinyString(d, "TAPE", label_x2, label_y2, !tape_selected);

        const int fader_offset = 8;
        const int fader_x = block_x + block_w + kGap + fader_offset;
        const int fader_w = kDisplayW - fader_x - kMargin;
        if(fader_w > 4)
        {
            const char* fader_labels[3] = {(t.mod_mode == 1) ? "DROP" : "DPTH",
                                           (t.mod_mode == 1) ? "RATE" : "SPD",
                                           "MIX"};
            const float fader_values[3] = {(t.mod_mode == 1) ? t.mod_wow : t.lfo_depth,
                                           (t.mod_mode == 1) ? t.tape_rate : t.mod_rate_hz,
                                           t.mod_mix};
            int param_index = selected_param;
            const bool fader_select_active = (param_index >= 0 && param_index < 3);
            if(!fader_select_active && !algo_selected) param_index = 0;
            const int fader_offsets[3] = {0, 0, 0};
            DrawVerticalFadersInRect(d, fader_x, block_y, fader_w, block_h,
                                     fader_labels, fader_values, 3, fader_select_active, param_index,
                                     fader_offsets, nullptr, nullptr, nullptr);
        }
    }
    else if(index == 2)
    {
        constexpr int kMargin = 2;
        const int block_y = Font5x7::H + 4;
        int block_h = kDisplayH - block_y - kMargin;
        if(block_h < 3) block_h = 3;
        const int fader_x = kMargin;
        const int fader_w = kDisplayW - (kMargin * 2);
        if(fader_w > 4)
        {
            const char* fader_labels[kDelayFaderCount] = {"TIM", "FBK", "SPRD", "FRZ", "MIX"};
            const float fader_values[kDelayFaderCount] = {t.delay_time, t.delay_feedback, t.delay_spread, 0.0f, t.delay_mix};
            int param_index = selected_param;
            const bool fader_select_active = (param_index >= 0 && param_index < kDelayFaderCount);
            if(!fader_select_active) param_index = 0;
            const bool hide_handles[kDelayFaderCount] = {false, false, false, true, false};
            const bool hide_rails[kDelayFaderCount] = {false, false, false, true, false};
            const int fader_offsets[kDelayFaderCount] = {0, 0, 0, 0, 0};
            DrawVerticalFadersInRect(d, fader_x, block_y, fader_w, block_h,
                                     fader_labels, fader_values, kDelayFaderCount, fader_select_active, param_index,
                                     fader_offsets, nullptr, hide_rails, hide_handles);

            const int label_y = block_y + block_h - Font5x7::H - 1;
            const int line_top = block_y + 2;
            const int line_bottom = label_y - 2;
            const int fader_left = fader_x + 2;
            const int fader_right = fader_x + fader_w - 3;
            const int span_x = fader_right - fader_left;
            int line_x = fader_left;
            if(kDelayFaderCount > 1 && span_x > 0)
                line_x = fader_left + (span_x * 3) / (kDelayFaderCount - 1);
            const char* frz_label = "FRZ";
            const int frz_w = TinyStringWidth(frz_label);
            int frz_x = line_x - (frz_w / 2);
            if(frz_x < fader_x + 1) frz_x = fader_x + 1;
            if(frz_x + frz_w > fader_x + fader_w - 2) frz_x = fader_x + fader_w - 2 - frz_w;
            line_x = frz_x + (frz_w / 2);
            const bool freeze_on = (t.delay_freeze >= 0.5f);
            const char* on_label = "ON";
            const char* off_label = "OFF";
            const int on_w = TinyStringWidth(on_label);
            const int off_w = TinyStringWidth(off_label);
            const int text_x_on = line_x - (on_w / 2);
            const int text_x_off = line_x - (off_w / 2);
            const int state_gap = 2;
            const int text_y_on = line_top + 1;
            const int text_y_off = text_y_on + Font5x7::H + state_gap;
            const int text_top = text_y_on - 1;
            const int text_bottom = text_y_off + Font5x7::H + 1;
            const bool highlight = (fader_select_active && param_index == 3);

            auto DrawSnowflake = [&](int sx, int sy, bool on)
            {
                d.DrawPixel(sx, sy, on);
                d.DrawPixel(sx - 1, sy, on);
                d.DrawPixel(sx + 1, sy, on);
                d.DrawPixel(sx, sy - 1, on);
                d.DrawPixel(sx, sy + 1, on);
            };

            const int area_left = line_x - 6;
            const int area_right = line_x + 6;
            const int area_top = line_top;
            const int area_bottom = line_bottom;
            const int area_w = area_right - area_left + 1;
            const int area_h = area_bottom - area_top + 1;
            if(area_w > 4 && area_h > 4 && freeze_on)
            {
                for(int i = 0; i < 6; ++i)
                {
                    const int sx = area_left + static_cast<int>((now_ms / 120 + i * 7) % area_w);
                    const int sy = area_top + static_cast<int>((now_ms / 60 + i * 9) % area_h);
                    if(sy < text_top || sy > text_bottom)
                        DrawSnowflake(sx, sy, true);
                }
            }

            if(freeze_on)
            {
                if(highlight)
                {
                    d.DrawRect(text_x_on - 1, text_y_on - 1, text_x_on + on_w, text_y_on + Font5x7::H, true, true);
                    DrawTinyString(d, on_label, text_x_on, text_y_on, false);
                }
                else DrawTinyString(d, on_label, text_x_on, text_y_on, true);
                DrawTinyString(d, off_label, text_x_off, text_y_off, true);
            }
            else
            {
                DrawTinyString(d, on_label, text_x_on, text_y_on, true);
                if(highlight)
                {
                    d.DrawRect(text_x_off - 1, text_y_off - 1, text_x_off + off_w, text_y_off + Font5x7::H, true, true);
                    DrawTinyString(d, off_label, text_x_off, text_y_off, false);
                }
                else DrawTinyString(d, off_label, text_x_off, text_y_off, true);
            }
        }
    }
    else if(index == 3)
    {
        constexpr int kMargin = 2;
        const int block_y = Font5x7::H + 4;
        int block_h = kDisplayH - block_y - kMargin;
        if(block_h < 3) block_h = 3;
        const int fader_x = kMargin;
        const int fader_w = kDisplayW - (kMargin * 2);
        if(fader_w > 4)
        {
            const char* fader_labels[kReverbFaderCount] = {"Pre", "Dmp", "Dcy", "DIR", "Wet"};
            const float fader_values[kReverbFaderCount] = {t.reverb_pre, t.reverb_damp, t.reverb_decay, t.reverb_reverse ? 1.0f : 0.0f, t.reverb_mix};
            int param_index = selected_param;
            const bool fader_select_active = (param_index >= 0 && param_index < kReverbFaderCount);
            if(!fader_select_active) param_index = 0;
            const bool hide_handles[kReverbFaderCount] = {false, false, false, true, false};
            const bool hide_rails[kReverbFaderCount] = {false, false, false, true, false};
            const int fader_offsets[kReverbFaderCount] = {0, 1, -1, 0, 0};
            DrawVerticalFadersInRect(d, fader_x, block_y, fader_w, block_h,
                                     fader_labels, fader_values, kReverbFaderCount, fader_select_active, param_index,
                                     fader_offsets, nullptr, hide_rails, hide_handles);

            const int label_y = block_y + block_h - Font5x7::H - 1;
            const int line_top = block_y + 2;
            const int line_bottom = label_y - 2;
            const int fader_left = fader_x + 2;
            const int fader_right = fader_x + fader_w - 3;
            const int span_x = fader_right - fader_left;
            int line_x = fader_left;
            if(kReverbFaderCount > 1 && span_x > 0)
                line_x = fader_left + (span_x * 3) / (kReverbFaderCount - 1);
            const char* dir_label = "DIR";
            const int dir_w = TinyStringWidth(dir_label);
            int dir_x = line_x - (dir_w / 2);
            if(dir_x < fader_x + 1) dir_x = fader_x + 1;
            if(dir_x + dir_w > fader_x + fader_w - 2) dir_x = fader_x + fader_w - 2 - dir_w;
            line_x = dir_x + (dir_w / 2);
            const bool reverse_on = t.reverb_reverse;
            const char* on_label = "REV";
            const char* off_label = "FOR";
            const int on_w = TinyStringWidth(on_label);
            const int off_w = TinyStringWidth(off_label);
            const int text_y_on = line_top + 1;
            const int text_y_off = text_y_on + Font5x7::H + 2;
            const int text_bottom = text_y_off + Font5x7::H + 1;
            const int text_x_on = line_x - (on_w / 2);
            const int text_x_off = line_x - (off_w / 2);
            if(reverse_on)
            {
                d.DrawRect(text_x_on - 1, text_y_on - 1, text_x_on + on_w, text_y_on + Font5x7::H, true, true);
                DrawTinyString(d, on_label, text_x_on, text_y_on, false);
                DrawTinyString(d, off_label, text_x_off, text_y_off, true);
            }
            else
            {
                DrawTinyString(d, on_label, text_x_on, text_y_on, true);
                d.DrawRect(text_x_off - 1, text_y_off - 1, text_x_off + off_w, text_y_off + Font5x7::H, true, true);
                DrawTinyString(d, off_label, text_x_off, text_y_off, false);
            }
            if(reverse_on)
            {
                const int area_left = line_x - 6;
                const int area_right = line_x + 6;
                const int area_top = line_top;
                const int area_bottom = line_bottom;
                const int area_w = area_right - area_left + 1;
                const int area_h = area_bottom - area_top + 1;
                if(area_w > 4 && area_h > 4)
                {
                    const int icon_top = (text_bottom + 1 > area_top) ? (text_bottom + 1) : area_top;
                    const int icon_bottom = area_bottom;
                    const int icon_h = icon_bottom - icon_top + 1;
                    if(icon_h >= 7)
                    {
                        const int cx = line_x + 3;
                        const int cy = icon_top + (icon_h / 2);
                        const int travel = 6;
                        int phase = static_cast<int>((now_ms / 100) % (travel + 2));
                        int shift = travel - phase;
                        if(shift < 0) shift = travel;
                        d.DrawLine(cx - 8, cy - 3, cx - 8, cy + 3, true);
                        const int tri_shift = shift;
                        d.DrawPixel(cx - 1 - tri_shift, cy, true);
                        d.DrawPixel(cx - tri_shift, cy - 1, true);
                        d.DrawPixel(cx - tri_shift, cy, true);
                        d.DrawPixel(cx - tri_shift, cy + 1, true);
                        d.DrawPixel(cx + 1 - tri_shift, cy - 2, true);
                        d.DrawPixel(cx + 1 - tri_shift, cy - 1, true);
                        d.DrawPixel(cx + 1 - tri_shift, cy, true);
                        d.DrawPixel(cx + 1 - tri_shift, cy + 1, true);
                        d.DrawPixel(cx + 1 - tri_shift, cy + 2, true);
                        d.DrawPixel(cx + 2 - tri_shift, cy - 3, true);
                        d.DrawPixel(cx + 2 - tri_shift, cy - 2, true);
                        d.DrawPixel(cx + 2 - tri_shift, cy - 1, true);
                        d.DrawPixel(cx + 2 - tri_shift, cy, true);
                        d.DrawPixel(cx + 2 - tri_shift, cy + 1, true);
                        d.DrawPixel(cx + 2 - tri_shift, cy + 2, true);
                        d.DrawPixel(cx + 2 - tri_shift, cy + 3, true);
                        d.DrawPixel(cx + 3 - tri_shift, cy, true);
                        d.DrawPixel(cx + 4 - tri_shift, cy - 1, true);
                        d.DrawPixel(cx + 4 - tri_shift, cy, true);
                        d.DrawPixel(cx + 4 - tri_shift, cy + 1, true);
                        d.DrawPixel(cx + 5 - tri_shift, cy - 2, true);
                        d.DrawPixel(cx + 5 - tri_shift, cy - 1, true);
                        d.DrawPixel(cx + 5 - tri_shift, cy, true);
                        d.DrawPixel(cx + 5 - tri_shift, cy + 1, true);
                        d.DrawPixel(cx + 5 - tri_shift, cy + 2, true);
                        d.DrawPixel(cx + 6 - tri_shift, cy - 3, true);
                        d.DrawPixel(cx + 6 - tri_shift, cy - 2, true);
                        d.DrawPixel(cx + 6 - tri_shift, cy - 1, true);
                        d.DrawPixel(cx + 6 - tri_shift, cy, true);
                        d.DrawPixel(cx + 6 - tri_shift, cy + 1, true);
                        d.DrawPixel(cx + 6 - tri_shift, cy + 2, true);
                        d.DrawPixel(cx + 6 - tri_shift, cy + 3, true);
                    }
                }
            }
        }
    }
}

static void PerformProcess_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display || !ctx.params)
        return;

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);

    OledPager& d = *ctx.display;
    d.Fill(false);

    if(app.perform_process_detail_active)
    {
        const uint8_t cursor = app.perform_process_fx_cursor & 0x03u;
        const uint8_t fx_id = app.perform_process_fx_order[cursor];
        const uint8_t pidx = app.perform_process_detail_param[cursor];
        const PerformParamsTargets& t = ctx.params->TargetsForUI();
        DrawFxDetailScreen(d, t, fx_id, pidx, ctx.now_ms);
        return;
    }

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    UiDraw_Header(d, layout, "MASTER FX BUS", status);

    const uint8_t main_cursor = static_cast<uint8_t>(app.perform_process_main_cursor % 6u);
    const int32_t selected_index = (main_cursor >= 2u) ? static_cast<int32_t>(main_cursor - 2u) : -1;
    const int box_y = layout.y_body;
    const int box_h = layout.y_footer - layout.y_body + layout.line_h;

    // Left pane: master volume UI placeholders (display-only for now).
    constexpr int kLeftX = 0;
    constexpr int kLeftW = 60;
    const int left_y = box_y;
    const int left_h = box_h;
    if(left_h > 6)
    {
        const int gap = 2;
        const int block_h = (left_h - gap) / 2;
        const int block_w = kLeftW - 1;
        const int x0 = kLeftX;
        const int x1 = x0 + block_w - 1;
        const int ay0 = left_y;
        const int ay1 = ay0 + block_h - 1;
        const int by0 = ay1 + gap + 1;
        const int by1 = by0 + block_h - 1;
        const bool sel_a = (main_cursor == 0u);
        const bool sel_b = (main_cursor == 1u);

        d.DrawRect(x0, ay0, x1, ay1, true, sel_a);
        d.DrawRect(x0, by0, x1, by1, true, sel_b);

        char a_buf[12];
        char b_buf[12];
        FormatDb(app.engine_gain_db[0], a_buf, sizeof(a_buf));
        FormatDb(app.engine_gain_db[1], b_buf, sizeof(b_buf));
        const char* a_text = sel_a ? a_buf : "VOL A";
        const char* b_text = sel_b ? b_buf : "VOL B";

        const int a_w = static_cast<int>(std::strlen(a_text)) * 6;
        const int b_w = static_cast<int>(std::strlen(b_text)) * 6;
        int a_x = x0 + ((block_w - a_w) / 2);
        int b_x = x0 + ((block_w - b_w) / 2);
        if(a_x < x0 + 1) a_x = x0 + 1;
        if(b_x < x0 + 1) b_x = x0 + 1;
        const int a_y = ay0 + ((block_h - 8) / 2);
        const int b_y = by0 + ((block_h - 8) / 2);
        d.SetCursor(a_x, a_y);
        d.WriteString(a_text, Font_6x8, !sel_a);
        d.SetCursor(b_x, b_y);
        d.WriteString(b_text, Font_6x8, !sel_b);
    }

    // Keep right half for FX faders.
    constexpr int kPaneX = 60;
    constexpr int kPaneW = 64;
    const PerformParamsTargets& t = ctx.params->TargetsForUI();
    const char* labels[4] = {"S", "M", "D", "R"};
    float values[4] = {};
    for(int i = 0; i < 4; ++i)
    {
        const uint8_t fx_id = app.perform_process_fx_order[i];
        switch(fx_id)
        {
            case 0: labels[i] = "S"; values[i] = Clamp01(t.sat_drive); break;
            case 1: labels[i] = "M"; values[i] = Clamp01(t.lfo_depth); break;
            case 2: labels[i] = "D"; values[i] = Clamp01(t.delay_mix); break;
            case 3:
            default:
                labels[i] = "R";
                values[i] = Clamp01(t.reverb_mix);
                break;
        }
    }

    const int fader_x = kPaneX;
    const int fader_w = kPaneW;
    const int fader_y = box_y + 1;
    const int fader_h = box_h - 2;
    if(fader_w > 4 && fader_h > 4)
        DrawVerticalFadersInRect(d, fader_x, fader_y, fader_w, fader_h, labels, values, 4, true, selected_index);
}

UiScreenId UiNav_Active(const UiNav& nav)
{
    return nav.stack[nav.top];
}

bool UiNav_Push(UiNav& nav, UiScreenId next)
{
    if(nav.top + 1 >= kUiStackMax)
        return false;
    nav.top++;
    nav.stack[nav.top] = next;
    return true;
}

bool UiNav_Pop(UiNav& nav)
{
    if(nav.top == 0)
        return false;
    nav.top--;
    return true;
}

static const UiMenuItem kHudMenuItems[] = {
    {"SD BROWSE", UiScreenId::SdBrowse, UiReqType::None},
    {"SAMPLE EDIT", UiScreenId::SampleEdit, UiReqType::None},
    {"SAVE PROJECT", UiScreenId::COUNT, UiReqType::SaveProject},
    {"LOAD PROJECT", UiScreenId::COUNT, UiReqType::LoadProject},
    {"FX", UiScreenId::Fx, UiReqType::None},
    {"MOD", UiScreenId::Mod, UiReqType::None},
    {"MACRO", UiScreenId::Macro, UiReqType::None},
    {"REBUILD", UiScreenId::COUNT, UiReqType::RebuildCache},
    {"LOAD", UiScreenId::COUNT, UiReqType::LoadSample},
    {"SAVE", UiScreenId::COUNT, UiReqType::SavePreset},
};

static void EnsureHudMenu(AppState& app)
{
    if(app.hud_menu_inited)
        return;
    UiListMenu_Init(app.hud_menu,
                    kHudMenuItems,
                    static_cast<uint8_t>(sizeof(kHudMenuItems) / sizeof(kHudMenuItems[0])),
                    3);
    app.hud_menu_inited = true;
}

static bool Hud_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    EnsureHudMenu(*ctx.app);
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod)
    {
        if(UiListMenu_OnEnc(ctx.app->hud_menu, e.value))
        {
            ctx.app->ui_dirty = true;
            return true;
        }
        return false;
    }
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        const UiMenuItem& item = ctx.app->hud_menu.items[ctx.app->hud_menu.cursor];
        if(item.req != UiReqType::None)
        {
            UiReq req{item.req, 0, 0};
            UiReq_Push(*ctx.app, req);
            ctx.app->ui_dirty = true;
            return true;
        }
        if(item.screen != UiScreenId::COUNT && UiNav_Push(ctx.app->ui_nav, item.screen))
            ctx.app->ui_dirty = true;
        return true;
    }

    return false;
}

static void Hud_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const AppState& app = *ctx.app;
    EnsureHudMenu(*ctx.app);

    const uint32_t peak_cycles   = app.audio_cycles_peak.load(std::memory_order_relaxed);
    const uint32_t budget_cycles = app.audio_budget_cycles.load(std::memory_order_relaxed);
    uint32_t cpu_pct = 0;
    if(budget_cycles > 0)
        cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
    if(cpu_pct > 999u)
        cpu_pct = 999u;
    const uint32_t late_cnt = app.audio_late_count.load(std::memory_order_relaxed);

    const uint32_t ovf_mod = app.ui_in_ovf % 1000;
    uint32_t hi = app.ui_in_hi;
    if(hi > 99u)
        hi = 99u;
    const char* sd_ok = app.sd.sd_ok ? "OK" : "ER";
    uint32_t wavs = app.sd.wav_count;
    if(wavs > 99u)
        wavs = 99u;
    const uint32_t ld = app.sd.load_in_progress ? app.sd.load_progress : 0;

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    UiDraw_Header(d, layout, "HUD", status);

    char buf[32];
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf,
                  sizeof(buf),
                  "U:%02lu C:%04lu CPU:%03lu",
                  (unsigned long)app.ui_hz,
                  (unsigned long)app.ctrl_hz,
                  (unsigned long)cpu_pct);
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    std::snprintf(buf, sizeof(buf), "LATE:%lu UIQO:%03lu H:%02lu",
                  (unsigned long)late_cnt,
                  (unsigned long)ovf_mod,
                  (unsigned long)hi);
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    std::snprintf(buf, sizeof(buf), "SD:%s W:%02lu L:%03lu",
                  sd_ok,
                  (unsigned long)wavs,
                  (unsigned long)ld);
    d.WriteString(buf, Font_6x8, true);

    UiListMenu_Render(ctx.app->hud_menu,
                      d,
                      layout.x,
                      layout.y_body + layout.line_h * 3,
                      layout.line_h);

    const char* footer = (app.project_status[0] != '\0')
                             ? app.project_status
                             : "EXT:SEL EXT:ENT P2:BACK";
    UiDraw_Footer(d, layout, footer);
}

static bool Fx_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app || !ctx.params)
        return false;

    static constexpr uint8_t kFxFieldCount = 4;
    static constexpr float kLpfMinHz = 80.0f;
    static constexpr float kLpfMaxHz = 12000.0f;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && !ctx.app->value_edit.active)
    {
        int next = static_cast<int>(ctx.app->fx_field_cursor) + e.value;
        while(next < 0) next += kFxFieldCount;
        while(next >= kFxFieldCount) next -= kFxFieldCount;
        ctx.app->fx_field_cursor = static_cast<uint8_t>(next);
        ctx.app->ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(!ctx.app->value_edit.active)
        {
            const auto& t = ctx.params->TargetsForUI();
            int16_t start_i = 0;
            UiValueSpec spec{};
            const char* label = "";
            switch(ctx.app->fx_field_cursor)
            {
                case 0: // Delay On
                    label = "DLY";
                    spec = {UiValueType::Bool, 1, 0, 1, nullptr, 0};
                    start_i = t.delay_on ? 1 : 0;
                    break;
                case 1: // Delay Mix
                    label = "MIX";
                    spec = {UiValueType::Norm01, 1, 0, 100, nullptr, 0};
                    start_i = (int16_t)(Clamp01(t.delay_mix) * 100.0f + 0.5f);
                    break;
                case 2: // Sat On
                    label = "SAT";
                    spec = {UiValueType::Bool, 1, 0, 1, nullptr, 0};
                    start_i = t.sat_on ? 1 : 0;
                    break;
                case 3: // LPF
                default:
                {
                    label = "LPF";
                    spec = {UiValueType::Norm01, 1, 0, 100, nullptr, 0};
                    float hz = t.lpf_cutoff_hz;
                    if(hz < kLpfMinHz) hz = kLpfMinHz;
                    if(hz > kLpfMaxHz) hz = kLpfMaxHz;
                    const float ratio = kLpfMaxHz / kLpfMinHz;
                    float norm = std::log(hz / kLpfMinHz) / std::log(ratio);
                    start_i = (int16_t)(Clamp01(norm) * 100.0f + 0.5f);
                    break;
                }
            }
            UiValueEdit_Begin(ctx.app->value_edit, label, spec, start_i);
            ctx.app->ui_dirty = true;
        }
        else
        {
            PerformParamsTargets& t = ctx.params->EditTargets();
            const int16_t v = ctx.app->value_edit.value_i;
            switch(ctx.app->fx_field_cursor)
            {
                case 0:
                    t.delay_on = (v != 0);
                    break;
                case 1:
                    t.delay_mix = Clamp01((float)v / 100.0f);
                    break;
                case 2:
                    t.sat_on = (v != 0);
                    break;
                case 3:
                default:
                {
                    const float ratio = kLpfMaxHz / kLpfMinHz;
                    const float norm = Clamp01((float)v / 100.0f);
                    t.lpf_cutoff_hz = kLpfMinHz * std::pow(ratio, norm);
                    break;
                }
            }
            ctx.params->PublishTargets();
            UiValueEdit_Commit(ctx.app->value_edit);
            ctx.app->ui_dirty = true;
        }
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && ctx.app->value_edit.active)
    {
        if(UiValueEdit_OnEnc(ctx.app->value_edit, e.value))
            ctx.app->ui_dirty = true;
        return true;
    }

    if(ctx.app->value_edit.active)
        return true;

    return false;
}

static void Fx_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.params || !ctx.display)
        return;

    const auto& t = ctx.params->TargetsForUI();

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(*ctx.app, status, sizeof(status));
    UiDraw_Header(d, layout, "FX", status);

    char buf[32];
    const uint32_t lpf_hz = static_cast<uint32_t>(t.lpf_cutoff_hz + 0.5f);
    char lpf_buf[12];
    if(lpf_hz >= 1000)
        std::snprintf(lpf_buf, sizeof(lpf_buf), "%2luk", (unsigned long)((lpf_hz + 500) / 1000));
    else
        std::snprintf(lpf_buf, sizeof(lpf_buf), "%3lu", (unsigned long)lpf_hz);

    const uint8_t cursor = ctx.app->fx_field_cursor;
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf, sizeof(buf), "%c DLY:%c", (cursor == 0) ? '>' : ' ',
                  t.delay_on ? '1' : '0');
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    std::snprintf(buf, sizeof(buf), "%c MIX:%03d", (cursor == 1) ? '>' : ' ',
                  ToPct01(t.delay_mix));
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    std::snprintf(buf, sizeof(buf), "%c SAT:%c", (cursor == 2) ? '>' : ' ',
                  t.sat_on ? '1' : '0');
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 3);
    std::snprintf(buf, sizeof(buf), "%c LPF:%s", (cursor == 3) ? '>' : ' ',
                  lpf_buf);
    d.WriteString(buf, Font_6x8, true);

    UiValueEdit_Render(ctx.app->value_edit, d, layout.x, layout.y_body + layout.line_h * 4);

    const char* hint = ctx.app->value_edit.active ? "EXT:CHG EXT:OK P2:CANC"
                                                   : "EXT:MOVE EXT:EDIT P2:BACK";
    UiDraw_Footer(d, layout, hint);
}

static bool Mod_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    static constexpr uint8_t kModFieldCount = 4;
    static const char* kRouteLabels[kMaxModRoutes] = {"R0", "R1", "R2", "R3"};
    static const char* kDstLabels[2] = {"CUT", "PIT"};

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && !ctx.app->value_edit.active)
    {
        int next = static_cast<int>(ctx.app->mod_field_cursor) + e.value;
        while(next < 0) next += kModFieldCount;
        while(next >= kModFieldCount) next -= kModFieldCount;
        ctx.app->mod_field_cursor = static_cast<uint8_t>(next);
        ctx.app->ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(!ctx.app->value_edit.active)
        {
            const uint8_t r_idx = ctx.app->mod_route_selected % kMaxModRoutes;
            const ModRoute& r = ctx.app->mod_routes_ui[r_idx];
            int16_t start_i = 0;
            UiValueSpec spec{};
            const char* label = "";
            switch(ctx.app->mod_field_cursor)
            {
                case 0: // Route Select
                    label = "ROUTE";
                    spec = {UiValueType::Enum, 1, 0, (int16_t)(kMaxModRoutes - 1),
                            kRouteLabels, (uint8_t)kMaxModRoutes};
                    start_i = (int16_t)r_idx;
                    break;
                case 1: // Enable
                    label = "EN";
                    spec = {UiValueType::Bool, 1, 0, 1, nullptr, 0};
                    start_i = r.enabled ? 1 : 0;
                    break;
                case 2: // Amount
                    label = "AMT";
                    spec = {UiValueType::Bipolar1, 1, -100, 100, nullptr, 0};
                    start_i = (int16_t)(ClampSigned(r.amount) * 100.0f);
                    break;
                case 3: // Destination
                default:
                    label = "DST";
                    spec = {UiValueType::Enum, 1, 0, 1, kDstLabels, 2};
                    start_i = (int16_t)(r.dst ? 1 : 0);
                    break;
            }
            UiValueEdit_Begin(ctx.app->value_edit, label, spec, start_i);
            ctx.app->ui_dirty = true;
        }
        else
        {
            const int16_t v = ctx.app->value_edit.value_i;
            uint8_t r_idx = ctx.app->mod_route_selected % kMaxModRoutes;
            ModRoute& r = ctx.app->mod_routes_ui[r_idx];
            bool publish = false;

            switch(ctx.app->mod_field_cursor)
            {
                case 0:
                    ctx.app->mod_route_selected = (uint8_t)v % kMaxModRoutes;
                    break;
                case 1:
                    r.enabled = (v != 0) ? 1 : 0;
                    publish = true;
                    break;
                case 2:
                    r.amount = ClampSigned((float)v / 100.0f);
                    publish = true;
                    break;
                case 3:
                default:
                    r.dst = (v != 0) ? static_cast<uint8_t>(ModDest::Pitch)
                                     : static_cast<uint8_t>(ModDest::FilterCutoff);
                    publish = true;
                    break;
            }

            if(publish)
                ModMatrix_Publish(ctx.app->mod_matrix, ctx.app->mod_routes_ui);
            UiValueEdit_Commit(ctx.app->value_edit);
            ctx.app->ui_dirty = true;
        }
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && ctx.app->value_edit.active)
    {
        if(UiValueEdit_OnEnc(ctx.app->value_edit, e.value))
            ctx.app->ui_dirty = true;
        return true;
    }

    if(ctx.app->value_edit.active)
        return true;

    return false;
}

static void Mod_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const uint8_t r_idx = ctx.app->mod_route_selected % kMaxModRoutes;
    const ModRoute& r = ctx.app->mod_routes_ui[r_idx];
    int amt = (int)(r.amount * 100.0f);
    if(amt > 99) amt = 99;
    if(amt < -99) amt = -99;

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(*ctx.app, status, sizeof(status));
    UiDraw_Header(d, layout, "MOD", status);

    char buf[32];
    const uint8_t cursor = ctx.app->mod_field_cursor;
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf, sizeof(buf), "%c R:%u",
                  (cursor == 0) ? '>' : ' ',
                  (unsigned)r_idx);
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    std::snprintf(buf, sizeof(buf), "%c EN:%c",
                  (cursor == 1) ? '>' : ' ',
                  r.enabled ? '1' : '0');
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    std::snprintf(buf, sizeof(buf), "%c AMT:%+03d",
                  (cursor == 2) ? '>' : ' ',
                  amt);
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 3);
    std::snprintf(buf, sizeof(buf), "%c DST:%c",
                  (cursor == 3) ? '>' : ' ',
                  DstChar(r.dst));
    d.WriteString(buf, Font_6x8, true);

    UiValueEdit_Render(ctx.app->value_edit, d, layout.x, layout.y_body + layout.line_h * 4);

    const char* hint = ctx.app->value_edit.active ? "EXT:CHG EXT:OK P2:CANC"
                                                   : "EXT:MOVE EXT:EDIT P2:BACK";
    UiDraw_Footer(d, layout, hint);
}

static bool Macro_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    bool changed = false;
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod)
    {
        const uint8_t sel = ctx.app->macro_ui.selected % kNumMacros;
        float v = ctx.app->macro_ui.value[sel];
        v = Clamp01(v + (float)e.value * 0.02f);
        ctx.app->macro_ui.value[sel] = v;
        Macros_Publish(*ctx.app, ctx.app->macro_ui);
        changed = true;
    }

    if(changed)
    {
        ctx.app->ui_dirty = true;
        return true;
    }

    return false;
}

static void Macro_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const uint8_t sel = ctx.app->macro_ui.selected % kNumMacros;
    uint32_t mac_val = (uint32_t)(ctx.app->macro_ui.value[sel] * 100.0f + 0.5f);
    if(mac_val > 100)
        mac_val = 100;

    const char sel_char = (sel == 0) ? 'A' : 'B';

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(*ctx.app, status, sizeof(status));
    UiDraw_Header(d, layout, "MAC", status);

    char buf[32];
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf, sizeof(buf), "MAC %c V:%03lu",
                  sel_char,
                  (unsigned long)mac_val);
    d.WriteString(buf, Font_6x8, true);

    UiDraw_Footer(d, layout, "POD:VAL P2:BACK");
}

static void SdBrowse_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;

    SdBrowserState& sd = ctx.app->sd;
    const UiLayout layout = UiLayout_Default();
    const uint8_t rows = (layout.rows_body > 1) ? static_cast<uint8_t>(layout.rows_body - 1) : 1;
    if(!sd.menu_inited || sd.menu_rows != rows)
    {
        sd.menu_rows = rows;
        SdBrowser_RebuildMenu(sd);
    }

    if(!sd.scan_in_progress && !sd.scan_done)
    {
        UiReq req{UiReqType::ScanSdWavs, 0, 0};
        UiReq_Push(*ctx.app, req);
        sd.scan_in_progress = true;
        SdBrowser_SetStatus(sd, "SCANNING");
        ctx.app->ui_dirty = true;
    }
}

static bool SdBrowse_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    SdBrowserState& sd = ctx.app->sd;
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod)
    {
        if(UiListMenu_OnEnc(sd.menu, e.value))
        {
            ctx.app->ui_dirty = true;
            return true;
        }
        return false;
    }

    // EXT encoder click is ENTER (load or select for deletion).
    if(e.type == UiInputType::BtnDown && (e.id == kUiBtnExtEnc))
    {
        if(sd.wav_count > 0 && !sd.scan_in_progress)
        {
            const uint16_t idx = sd.menu.cursor;

            // If we're in delete mode, EXT click selects file and goes to confirm screen.
            if(ctx.app->sd_delete_mode)
            {
                ctx.app->sd_delete_index = idx;
                ExtractBaseName(sd.paths[idx],
                                ctx.app->sd_delete_name,
                                sizeof(ctx.app->sd_delete_name));
                UiNav_Push(ctx.app->ui_nav, UiScreenId::SdDeleteConfirm);
                ctx.app->ui_dirty = true;
                return true;
            }

            // Normal load behavior.
            if(ctx.app->engine_load_target_layer < kPerformLayerCount)
            {
                const uint8_t target = ctx.app->engine_load_target_layer & 1u;
                // Existing loader writes into inactive slot; force target by selecting opposite as current.
                ctx.app->sd_current_slot.store(target ^ 1u, std::memory_order_release);
                std::snprintf(ctx.app->engine_sample_path[target],
                              sizeof(ctx.app->engine_sample_path[target]),
                              "%s",
                              sd.paths[idx]);
                ExtractBaseName(sd.paths[idx],
                                ctx.app->engine_sample_name[target],
                                sizeof(ctx.app->engine_sample_name[target]));
            }
            UiReq req{UiReqType::LoadWavIndex, idx, 0};
            UiReq_Push(*ctx.app, req);
            sd.load_in_progress = true;
            sd.load_progress = 0;
            SdBrowser_SetStatus(sd, "LOADING");
            if(ctx.app->engine_load_from_perform)
            {
                UiNav_Pop(ctx.app->ui_nav);
            }
            ctx.app->ui_dirty = true;
        }
        return true;
    }

    return false;
}

static void SdBrowse_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    SdBrowserState& sd = ctx.app->sd;
    const UiLayout layout = UiLayout_Default();
    bool show_issue = false;
    char issue_buf[24];
    issue_buf[0] = '\0';
    if(!sd.sd_ok)
    {
        show_issue = true;
        std::snprintf(issue_buf, sizeof(issue_buf), "SD ERR");
    }
    else if(sd.scan_done && !sd.scan_in_progress && sd.wav_count == 0)
    {
        show_issue = true;
        std::snprintf(issue_buf, sizeof(issue_buf), "NO WAV");
    }
    else if(sd.status[0] != '\0')
    {
        // Keep non-error status hidden to keep browse view clean.
        const bool noisy_ok = (std::strncmp(sd.status, "LOADED", 6) == 0)
                           || (std::strncmp(sd.status, "LOADING", 7) == 0)
                           || (std::strncmp(sd.status, "SCANNING", 8) == 0)
                           || (std::strncmp(sd.status, "DELETED", 7) == 0);
        if(!noisy_ok)
        {
            show_issue = true;
            std::snprintf(issue_buf, sizeof(issue_buf), "%s", sd.status);
        }
    }

    uint8_t lines_used = static_cast<uint8_t>(1 + (show_issue ? 1 : 0));
    if(lines_used >= layout.rows_body)
        lines_used = layout.rows_body;

    uint8_t menu_rows = (layout.rows_body > lines_used)
                        ? static_cast<uint8_t>(layout.rows_body - lines_used)
                        : 1;
    if(!sd.menu_inited || sd.menu_rows != menu_rows)
    {
        sd.menu_rows = menu_rows;
        SdBrowser_RebuildMenu(sd);
    }

    OledPager& d = *ctx.display;
    d.Fill(false);

    char status[16];
    BuildStatus(*ctx.app, status, sizeof(status));
    UiDraw_Header(d, layout, "SD BROWSE", status);

    if(show_issue && lines_used > 1)
    {
        d.SetCursor(layout.x, layout.y_body);
        d.WriteString(issue_buf, Font_6x8, true);
    }

    UiListMenu_Render(sd.menu,
                      d,
                      layout.x,
                      layout.y_body + layout.line_h * lines_used,
                      layout.line_h);
}

// -------------------------
// SHIFT MENU (POD BUTTON1)
// -------------------------

static void ShiftMenu_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    // Returning to SHIFT should cancel any SD delete mode.
    ctx.app->sd_delete_mode = false;
    ctx.app->shift_menu_edit_volume = false;
}

static bool ShiftMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    if(ctx.shift)
        return false;

    AppState& app = *ctx.app;

    if(e.type == UiInputType::EncDelta)
    {
        // R encoder turn adjusts value when editing VOLUME.
        if(app.shift_menu_edit_volume && e.id == kUiEncExt)
        {
            if(!ctx.params)
                return true;
            auto& t = ctx.params->EditTargets();
            // Option B: allow master boost up to 200% (2.0)
            static constexpr float kMasterLevelMax = 2.0f;

            // Cuz-like feel: time-based acceleration.
            // Fast turns -> bigger jumps.
            static uint32_t s_last_t_ms = 0;
            const uint32_t now_ms = e.t_ms;
            const uint32_t dt_ms  = (s_last_t_ms == 0) ? 999u : (now_ms - s_last_t_ms);
            s_last_t_ms = now_ms;

            float accel = 1.0f;
            if(dt_ms <= 25)       accel = 10.0f;
            else if(dt_ms <= 50)  accel = 6.0f;
            else if(dt_ms <= 90)  accel = 3.0f;
            else if(dt_ms <= 140) accel = 2.0f;

            // Base step: 1% per detent at accel=1.0
            const float base_step = 0.01f;

            float next = t.master_level + (float)e.value * base_step * accel;

            if(next < 0.0f) next = 0.0f;
            if(next > kMasterLevelMax) next = kMasterLevelMax;

            t.master_level = next;
            ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }

        // L encoder turn scrolls between DELETE/VOLUME when not editing.
        if(!app.shift_menu_edit_volume && e.id == kUiEncPod)
        {
            uint8_t cur = app.shift_menu_cursor;
            if(e.value > 0)
                cur = (cur < 1) ? (uint8_t)(cur + 1) : cur;
            else if(e.value < 0)
                cur = (cur > 0) ? (uint8_t)(cur - 1) : cur;

            if(cur != app.shift_menu_cursor)
            {
                app.shift_menu_cursor = cur;
                app.ui_dirty = true;
            }
            return true;
        }
    }
    // EXT encoder click = select.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(app.shift_menu_cursor == 0)
        {
            // DELETE: enter SD browser in delete mode.
            app.sd_delete_mode = true;
            app.shift_menu_edit_volume = false;
            SdBrowser_SetStatus(app.sd, "DEL:SELECT");
            UiNav_Push(app.ui_nav, UiScreenId::SdBrowse);
            app.ui_dirty = true;
            return true;
        }
        else
        {
            // VOLUME: toggle edit mode.
            app.shift_menu_edit_volume = !app.shift_menu_edit_volume;
            app.ui_dirty = true;
            return true;
        }
    }

    // L encoder click backs out one level when editing volume.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        if(app.shift_menu_edit_volume)
        {
            app.shift_menu_edit_volume = false;
            app.ui_dirty = true;
            return true; // consume so router doesn't pop screen
        }
    }

    // POD2 also cancels volume edit (optional)
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        if(app.shift_menu_edit_volume)
        {
            app.shift_menu_edit_volume = false;
            app.ui_dirty = true;
            return true;
        }
    }

    return false;
}

static void ShiftMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    const int screen_w = (int)d.Width();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    UiDraw_Header(d, layout, "SETTINGS", status);

    // Compute volume percent for display (0..200 with boost).
    uint32_t vol_pct = 0;
    if(ctx.params)
    {
        static constexpr float kMasterLevelMax = 2.0f;
        float v = ctx.params->TargetsForUI().master_level;

        if(v < 0.0f) v = 0.0f;
        if(v > kMasterLevelMax) v = kMasterLevelMax;

        vol_pct = (uint32_t)(v * 100.0f + 0.5f);
        if(vol_pct > 200u)
            vol_pct = 200u;
    }

    // Two rows: DELETE and VOLUME.
    const int row_y0 = layout.y_body;
    const int row_h = layout.line_h;

    for(int i = 0; i < 2; ++i)
    {
        const bool sel = (app.shift_menu_cursor == (uint8_t)i);
        const int y = row_y0 + i * row_h;
        const int x0 = layout.x;
        const int y1 = y + row_h - 1;

        // Highlight only the label area (not the numeric value).
        const int label_x1 = ((x0 + 60) < (screen_w - 1)) ? (x0 + 60) : (screen_w - 1);
        if(sel)
            d.DrawRect(x0, y, label_x1, y1, true, true);

        d.SetCursor(x0 + 1, y + 1);
        if(i == 0)
        {
            d.WriteString("DELETE", Font_6x8, !sel);
        }
        else
        {
            const char* label = app.shift_menu_edit_volume ? "VOLUME*" : "VOLUME";
            d.WriteString(label, Font_6x8, !sel);

            // Right-aligned value.
            char buf[8];
            int  val_len = 3;
            if(vol_pct == 100u)
            {
                std::snprintf(buf, sizeof(buf), "UNITY");
                val_len = 5;
            }
            else if(vol_pct > 100u)
            {
                std::snprintf(buf, sizeof(buf), "+%3lu", (unsigned long)vol_pct);
                val_len = 4;
            }
            else
            {
                std::snprintf(buf, sizeof(buf), "%3lu", (unsigned long)vol_pct);
                val_len = 3;
            }

            const int val_w = 6 * val_len;
            d.SetCursor(screen_w - val_w - 1, y + 1);
            d.WriteString(buf, Font_6x8, true);
        }
    }

    //UiDraw_Footer(d, layout, "R:SEL  L:BACK");
}

// -------------------------
// SD DELETE CONFIRM SCREEN
// -------------------------

static bool SdDeleteConfirm_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return false;

    AppState& app = *ctx.app;
    SdBrowserState& sd = app.sd;

    const uint16_t idx = app.sd_delete_index;
    if(!app.sd_delete_mode || idx >= sd.wav_count || sd.scan_in_progress)
    {
        SdBrowser_SetStatus(sd, "DEL ERR");
        // Return to browser.
        UiNav_Pop(app.ui_nav);
        app.sd_delete_mode = false;
        app.ui_dirty = true;
        return true;
    }

    // Queue delete + rescan.
    UiReq del{UiReqType::DeleteWavIndex, idx, 0};
    UiReq scan{UiReqType::ScanSdWavs, 0, 0};
    (void)UiReq_Push(app, del);
    (void)UiReq_Push(app, scan);

    sd.scan_in_progress = true;
    sd.scan_done = false;
    SdBrowser_SetStatus(sd, "DELETING");

    // Exit delete mode after one delete (safer).
    app.sd_delete_mode = false;

    // Pop confirm screen back to SD list.
    UiNav_Pop(app.ui_nav);
    app.ui_dirty = true;
    return true;
}

static void SdDeleteConfirm_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;
    AppState& app = *ctx.app;
    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    UiDraw_Header(d, layout, "DELETE WAV", status);

    // Filename line
    d.SetCursor(layout.x, layout.y_body);
    char namebuf[32];
    if(app.sd_delete_name[0] != '\0')
        std::snprintf(namebuf, sizeof(namebuf), "%s", app.sd_delete_name);
    else
        std::snprintf(namebuf, sizeof(namebuf), "(no file)");
    d.WriteString(namebuf, Font_6x8, true);

    // Prompt lines (match Cuz style: R=YES, L=NO)
    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    d.WriteString("ARE YOU SURE?", Font_6x8, true);
    d.SetCursor(layout.x, layout.y_body + layout.line_h * 3);
    d.WriteString("R:YES   L:NO", Font_6x8, true);
}

enum SampleEditItem : uint8_t
{
    SE_TrimStart = 0,
    SE_TrimEnd,
    SE_LoopEnable,
    SE_LoopStart,
    SE_LoopEnd,
    SE_Normalize,
    SE_LoopFind,
    SE_SaveWav,
    SE_Count
};

static void EnsureSampleEditMenu(AppState& app, uint8_t rows)
{
    static const UiMenuItem items[] = {
        {"TRIM START", UiScreenId::COUNT, UiReqType::None},
        {"TRIM END", UiScreenId::COUNT, UiReqType::None},
        {"LOOP EN", UiScreenId::COUNT, UiReqType::None},
        {"LOOP START", UiScreenId::COUNT, UiReqType::None},
        {"LOOP END", UiScreenId::COUNT, UiReqType::None},
        {"NORMALIZE", UiScreenId::COUNT, UiReqType::None},
        {"LOOP FIND", UiScreenId::COUNT, UiReqType::None},
        {"SAVE WAV", UiScreenId::COUNT, UiReqType::None},
    };

    if(app.sample_edit_menu_inited && app.sample_edit_menu.rows == rows)
        return;
    UiListMenu_Init(app.sample_edit_menu,
                    items,
                    static_cast<uint8_t>(sizeof(items) / sizeof(items[0])),
                    rows);
    app.sample_edit_menu_inited = true;
}

static uint32_t FramesToMs(uint32_t frames)
{
    return (frames + 24u) / 48u;
}

static uint32_t MsToFrames(uint32_t ms)
{
    return ms * 48u;
}

static bool SampleEdit_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    AppState& app = *ctx.app;
    if(ctx.shift)
        return false;

    const uint8_t slot = app.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    SampleEdit edit = app.sd_edit_slots[slot];
    const uint32_t frames = (slot < kSdSampleSlots && app.sd_slots[slot].length > 0)
                            ? app.sd_slots[slot].length
                            : 0;

    const UiLayout layout = UiLayout_Default();
    const uint8_t info_lines = 4;
    const uint8_t rows = (layout.rows_body > info_lines)
                             ? static_cast<uint8_t>(layout.rows_body - info_lines)
                             : 1;
    EnsureSampleEditMenu(app, rows);

    if(app.value_edit.active)
    {
        if(e.type == UiInputType::EncDelta && e.id == kUiEncExt)
        {
            if(UiValueEdit_OnEnc(app.value_edit, e.value))
                app.ui_dirty = true;
            return true;
        }
        if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
        {
            const int16_t v = app.value_edit.value_i;
            const uint32_t v_ms = (v < 0) ? 0u : static_cast<uint32_t>(v);
            const uint32_t v_frames = MsToFrames(v_ms);
            switch(app.sample_edit_menu.cursor)
            {
                case SE_TrimStart:
                    edit.start_frame = v_frames;
                    break;
                case SE_TrimEnd:
                    edit.end_frame = v_frames;
                    break;
                case SE_LoopEnable:
                    edit.loop_enable = (v != 0) ? 1 : 0;
                    break;
                case SE_LoopStart:
                    edit.loop_start = v_frames;
                    break;
                case SE_LoopEnd:
                    edit.loop_end = v_frames;
                    break;
                default:
                    break;
            }
            SampleEdit_Clamp(edit, frames);
            app.sd_edit_slots[slot] = edit;
            app.sd_edit_pending = edit;
            app.sd_edit_slot.store(slot, std::memory_order_release);
            app.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
            app.sd_edit_ready.store(1, std::memory_order_release);
            UiValueEdit_Commit(app.value_edit);
            app.ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt)
    {
        if(UiListMenu_OnEnc(app.sample_edit_menu, e.value))
        {
            app.ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::BtnDown
       && (e.id == kUiBtnExtEnc))
    {
        const uint8_t idx = app.sample_edit_menu.cursor;
        if(idx == SE_Normalize)
        {
            UiReq req{UiReqType::NormalizeCurrent, 0, 0};
            UiReq_Push(app, req);
            SdBrowser_SetStatus(app.sd, "NORMALIZE");
            app.ui_dirty = true;
            return true;
        }
        if(idx == SE_LoopFind)
        {
            UiReq req{UiReqType::LoopFindCurrent, 0, 0};
            UiReq_Push(app, req);
            SdBrowser_SetStatus(app.sd, "LOOP FIND");
            app.ui_dirty = true;
            return true;
        }
        if(idx == SE_SaveWav)
        {
            UiReq req{UiReqType::SaveRenderedWavCurrent, 0, 0};
            if(UiReq_Push(app, req))
            {
                SdBrowser_SetSaveStatus(app.sd, "SAVING");
                app.sd.save_progress = 0;
                app.sd.save_in_progress = true;
            }
            else
            {
                SdBrowser_SetSaveStatus(app.sd, "SAVE ERR");
            }
            app.ui_dirty = true;
            return true;
        }

        UiValueSpec spec{};
        const char* label = "";
        int16_t start_i = 0;
        switch(idx)
        {
            case SE_TrimStart:
                label = "TRIM S";
                spec = {UiValueType::Norm01, 1, 0, 5000, nullptr, 0};
                start_i = static_cast<int16_t>(FramesToMs(edit.start_frame));
                break;
            case SE_TrimEnd:
                label = "TRIM E";
                spec = {UiValueType::Norm01, 1, 0, 5000, nullptr, 0};
                start_i = static_cast<int16_t>(FramesToMs(edit.end_frame));
                break;
            case SE_LoopEnable:
                label = "LOOP";
                spec = {UiValueType::Bool, 1, 0, 1, nullptr, 0};
                start_i = edit.loop_enable ? 1 : 0;
                break;
            case SE_LoopStart:
                label = "LP S";
                spec = {UiValueType::Norm01, 1, 0, 5000, nullptr, 0};
                start_i = static_cast<int16_t>(FramesToMs(edit.loop_start));
                break;
            case SE_LoopEnd:
                label = "LP E";
                spec = {UiValueType::Norm01, 1, 0, 5000, nullptr, 0};
                start_i = static_cast<int16_t>(FramesToMs(edit.loop_end));
                break;
            default:
                return false;
        }
        UiValueEdit_Begin(app.value_edit, label, spec, start_i);
        app.ui_dirty = true;
        return true;
    }

    return false;
}

static void SampleEdit_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    const uint8_t slot = app.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const SampleEdit& edit = app.sd_edit_slots[slot];

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    UiDraw_Header(d, layout, "SAMPLE EDIT", status);

    const uint8_t info_lines = 4;
    const uint8_t rows = (layout.rows_body > info_lines)
                             ? static_cast<uint8_t>(layout.rows_body - info_lines)
                             : 1;
    EnsureSampleEditMenu(app, rows);

    const uint32_t start_ms = FramesToMs(edit.start_frame);
    const uint32_t end_ms = FramesToMs(edit.end_frame);
    const uint32_t loop_s_ms = FramesToMs(edit.loop_start);
    const uint32_t loop_e_ms = FramesToMs(edit.loop_end);
    uint32_t gain_pct = static_cast<uint32_t>(edit.gain * 100.0f + 0.5f);
    if(gain_pct > 999u)
        gain_pct = 999u;

    char info[32];
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(info, sizeof(info), "ST:%04lu EN:%04lu",
                  (unsigned long)start_ms,
                  (unsigned long)end_ms);
    d.WriteString(info, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    std::snprintf(info, sizeof(info), "LP:%c LS:%04lu",
                  edit.loop_enable ? '1' : '0',
                  (unsigned long)loop_s_ms);
    d.WriteString(info, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    std::snprintf(info, sizeof(info), "LE:%04lu G:%03lu",
                  (unsigned long)loop_e_ms,
                  (unsigned long)gain_pct);
    d.WriteString(info, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 3);
    if(app.sd.save_in_progress)
    {
        std::snprintf(info, sizeof(info), "SAVING %03u%%",
                      static_cast<unsigned>(app.sd.save_progress));
        d.WriteString(info, Font_6x8, true);
    }
    else if(app.sd.save_status[0] != '\0')
    {
        if(std::strncmp(app.sd.save_status, "SAVED", 5) == 0
           && app.sd.save_name[0] != '\0')
        {
            std::snprintf(info, sizeof(info), "SAVED:%s", app.sd.save_name);
        }
        else
        {
            std::snprintf(info, sizeof(info), "%s", app.sd.save_status);
        }
        d.WriteString(info, Font_6x8, true);
    }

    const uint8_t count = app.sample_edit_menu.count;
    for(uint8_t row = 0; row < app.sample_edit_menu.rows; ++row)
    {
        const uint8_t idx = static_cast<uint8_t>(app.sample_edit_menu.scroll + row);
        if(idx >= count)
            break;

        char buf[32];
        const char prefix = (idx == app.sample_edit_menu.cursor) ? '>' : ' ';
        switch(idx)
        {
            case SE_TrimStart:
                std::snprintf(buf, sizeof(buf), "%c TRIM S:%04lu",
                              prefix, (unsigned long)start_ms);
                break;
            case SE_TrimEnd:
                std::snprintf(buf, sizeof(buf), "%c TRIM E:%04lu",
                              prefix, (unsigned long)end_ms);
                break;
            case SE_LoopEnable:
                std::snprintf(buf, sizeof(buf), "%c LOOP EN:%c",
                              prefix, edit.loop_enable ? '1' : '0');
                break;
            case SE_LoopStart:
                std::snprintf(buf, sizeof(buf), "%c LOOP S:%04lu",
                              prefix, (unsigned long)loop_s_ms);
                break;
            case SE_LoopEnd:
                std::snprintf(buf, sizeof(buf), "%c LOOP E:%04lu",
                              prefix, (unsigned long)loop_e_ms);
                break;
            case SE_Normalize:
                std::snprintf(buf, sizeof(buf), "%c NORMALIZE", prefix);
                break;
            case SE_LoopFind:
                std::snprintf(buf, sizeof(buf), "%c LOOP FIND", prefix);
                break;
            case SE_SaveWav:
                std::snprintf(buf, sizeof(buf), "%c SAVE WAV", prefix);
                break;
            default:
                std::snprintf(buf, sizeof(buf), "%c -", prefix);
                break;
        }

        d.SetCursor(layout.x,
                    layout.y_body + static_cast<int>(row + info_lines) * layout.line_h);
        d.WriteString(buf, Font_6x8, true);
    }

    const bool busy = app.sd.save_in_progress
                      || (app.ui_req_busy
                          && (app.ui_req_active == UiReqType::NormalizeCurrent
                              || app.ui_req_active == UiReqType::LoopFindCurrent
                              || app.ui_req_active == UiReqType::SaveRenderedWavCurrent));
    const char* hint = busy ? "BUSY"
                            : (app.value_edit.active ? "EXT:CHG EXT:OK P2:CANC"
                                                     : "A=SEL  B=BACK");
    UiDraw_Footer(d, layout, hint);
}

const UiScreen& GetScreen(UiScreenId id)
{
    static const UiScreen start{UiScreenId::Start, nullptr, nullptr, MainMenu_OnEvent, MainMenu_Render, MainMenu_OnEnter};
    static const UiScreen presets{UiScreenId::Presets, nullptr, nullptr, Presets_OnEvent, Presets_Render};
    static const UiScreen record{UiScreenId::Record, Record_OnEnter, Record_OnExit, Record_OnEvent, Record_Render};
    static const UiScreen perform_menu{UiScreenId::PerformMenu, nullptr, nullptr, PerformMenu_OnEvent, PerformMenu_Render, PerformMenu_OnEnter};
    static const UiScreen perform_engine{UiScreenId::PerformEngine,
                                         PerformEngine_OnScreenEnter,
                                         nullptr,
                                         PerformEngine_OnEvent,
                                         PerformEngine_Render,
                                         PerformEngine_OnEnter};
    static const UiScreen perform_wave_edit{UiScreenId::PerformWaveEdit,
                                            nullptr,
                                            nullptr,
                                            PerformWaveEdit_OnEvent,
                                            PerformWaveEdit_Render};
    static const UiScreen perform_keyzone{UiScreenId::PerformKeyzone, nullptr, nullptr, PerformKeyzone_OnEvent, PerformKeyzone_Render};
    static const UiScreen perform_adsr{UiScreenId::PerformAdsr, nullptr, nullptr, PerformAdsr_OnEvent, PerformAdsr_Render};
    static const UiScreen perform_emphasis{UiScreenId::PerformEmphasis, nullptr, nullptr, PerformEmphasis_OnEvent, PerformEmphasis_Render};
    static const UiScreen perform_process{UiScreenId::PerformProcess, nullptr, nullptr, PerformProcess_OnEvent, PerformProcess_Render};
    static const UiScreen hud{UiScreenId::Hud, nullptr, nullptr, Hud_OnEvent, Hud_Render};
    static const UiScreen fx{UiScreenId::Fx, nullptr, nullptr, Fx_OnEvent, Fx_Render};
    static const UiScreen mod{UiScreenId::Mod, nullptr, nullptr, Mod_OnEvent, Mod_Render};
    static const UiScreen macro{UiScreenId::Macro, nullptr, nullptr, Macro_OnEvent, Macro_Render};
    static const UiScreen sd{UiScreenId::SdBrowse, SdBrowse_OnEnter, nullptr, SdBrowse_OnEvent, SdBrowse_Render};
    static const UiScreen sd_del_confirm{UiScreenId::SdDeleteConfirm,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        SdDeleteConfirm_Render,
                                        SdDeleteConfirm_OnEnter};
    static const UiScreen se{UiScreenId::SampleEdit, nullptr, nullptr, SampleEdit_OnEvent, SampleEdit_Render};
    static const UiScreen shift{UiScreenId::ShiftMenu,
                               ShiftMenu_OnScreenEnter,
                               nullptr,
                               ShiftMenu_OnEvent,
                               ShiftMenu_Render};

    switch(id)
    {
        case UiScreenId::Start:
            return start;
        case UiScreenId::Presets:
            return presets;
        case UiScreenId::Record:
            return record;
        case UiScreenId::PerformMenu:
            return perform_menu;
        case UiScreenId::PerformEngine:
            return perform_engine;
        case UiScreenId::PerformWaveEdit:
            return perform_wave_edit;
        case UiScreenId::PerformKeyzone:
            return perform_keyzone;
        case UiScreenId::PerformAdsr:
            return perform_adsr;
        case UiScreenId::PerformEmphasis:
            return perform_emphasis;
        case UiScreenId::PerformProcess:
            return perform_process;
        case UiScreenId::Hud:
            return hud;
        case UiScreenId::Fx:
            return fx;
        case UiScreenId::Mod:
            return mod;
        case UiScreenId::Macro:
            return macro;
        case UiScreenId::SdBrowse:
            return sd;
        case UiScreenId::SdDeleteConfirm:
            return sd_del_confirm;
        case UiScreenId::SampleEdit:
            return se;
        case UiScreenId::ShiftMenu:
            return shift;
        default:
            return hud;
    }
}

void UiRouter_DispatchEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return;

    const UiScreenId active = UiNav_Active(ctx.app->ui_nav);
    const UiScreen& s = GetScreen(active);

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        // Special case: when editing SETTINGS->VOLUME, "BACK" should exit edit mode
        // without leaving the SETTINGS screen.
        if(active == UiScreenId::ShiftMenu
           && ctx.app->shift_menu_edit_volume)
        {
            ctx.app->shift_menu_edit_volume = false;
            ctx.app->ui_dirty = true;
            return;
        }

        // Record uses BACK for in-screen state transitions (review/back-confirm/stop).
        if(active == UiScreenId::Record && s.OnEvent && s.OnEvent(ctx, e))
            return;

        // PROCESS detail uses BACK to return from FX detail to MASTER BUS,
        // not to leave the PROCESS screen entirely.
        if(active == UiScreenId::PerformProcess && s.OnEvent && s.OnEvent(ctx, e))
            return;

        if(UiNav_Pop(ctx.app->ui_nav))
            ctx.app->ui_dirty = true;
        return;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc && s.on_enter)
    {
        if(s.on_enter(ctx))
        {
            ctx.app->ui_dirty = true;
            return;
        }
    }

    if(s.OnEvent)
        s.OnEvent(ctx, e);
}

void UiRouter_Render(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    const UiScreen& s = GetScreen(UiNav_Active(ctx.app->ui_nav));
    if(s.Render)
        s.Render(ctx);
}













