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

static constexpr int32_t kMainMenuCount = 3;
static const char* kMenuLabels[kMainMenuCount] = {"LOAD", "RECORD", "PERFORM"};
static constexpr int32_t kPerformMenuCount = 4;
static const char* kPerformMenuLabels[kPerformMenuCount] = {"ENGINE", "KEYZONE", "ADSR", "EMPHASIS"};

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
    static const int32_t order[kPerformMenuCount] = {0, 1, 2, 3};
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
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFE, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0x03, 0xFE, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x03, 0xF8, 0xFF, 0x03, 0xFE, 0x00, 0x3C, 0x00,
    0x03, 0xF8, 0xFF, 0x03, 0xFF, 0x00, 0x3E, 0x00,
    0x03, 0x18, 0xC0, 0x00, 0x03, 0x80, 0x37, 0x00,
    0x03, 0x18, 0xC1, 0xFF, 0x81, 0xC0, 0x33, 0x80,
    0x03, 0x18, 0xC0, 0xFF, 0x00, 0xE0, 0x31, 0xC0,
    0x03, 0x1F, 0xC0, 0x78, 0x00, 0x70, 0x30, 0xC0,
    0x03, 0x1F, 0xC0, 0x3C, 0x00, 0x3F, 0xF0, 0xC0,
    0x03, 0x00, 0x00, 0x1E, 0x00, 0x1F, 0xF0, 0xC0,
    0x03, 0x00, 0x01, 0xFF, 0x00, 0x00, 0x00, 0xC0,
    0x03, 0x00, 0x00, 0xFF, 0x80, 0x00, 0x00, 0xC0,
    0x03, 0x1F, 0xC0, 0x7F, 0x80, 0x00, 0x00, 0xC0,
    0x03, 0x1F, 0xC0, 0x38, 0x00, 0x0F, 0xF0, 0xC0,
    0x03, 0x19, 0xC0, 0x1C, 0x00, 0x0F, 0xF0, 0xC0,
    0x03, 0x19, 0xC0, 0x0C, 0x00, 0x0C, 0x30, 0xC0,
    0x03, 0x19, 0xC0, 0x06, 0x00, 0x0C, 0x30, 0xC0,
    0x03, 0xF9, 0xFF, 0xC3, 0x00, 0x0C, 0x31, 0xC0,
    0x03, 0xF9, 0xFF, 0xE1, 0x00, 0x0C, 0x33, 0x80,
    0x00, 0x00, 0x00, 0xF0, 0x00, 0x0C, 0x37, 0x00,
    0x00, 0x00, 0x00, 0x7F, 0xFF, 0xFC, 0x3E, 0x00,
    0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFC, 0x3C, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
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
            return UiNav_Push(ctx.app->ui_nav, UiScreenId::SdBrowse);
        case 1:
            return UiNav_Push(ctx.app->ui_nav, UiScreenId::Fx);
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
        default:
            return UiNav_Push(ctx.app->ui_nav, UiScreenId::PerformEmphasis);
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
static constexpr int32_t kEngineRowCount = 4;
static constexpr int32_t kEngineRowLoad = 0;
static constexpr int32_t kEngineRowTune = 1;
static constexpr int32_t kEngineRowGain = 2;
static constexpr int32_t kEngineRowOneShot = 3;

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
        else if(row == kEngineRowGain)
        {
            int v = static_cast<int>(app.engine_gain_db[layer]) + e.value;
            v = ClampInt(v, -32, 6);
            const int8_t vv = static_cast<int8_t>(v);
            if(vv != app.engine_gain_db[layer])
            {
                app.engine_gain_db[layer] = vv;
                changed = true;
            }
        }
        else if(row == kEngineRowOneShot)
        {
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

    const char* name = sample_loaded ? app.engine_sample_name[layer] : "NONE";
    if(name == nullptr || name[0] == '\0')
        name = sample_loaded ? "LOADED" : "NONE";
    char name_buf[24];
    std::snprintf(name_buf, sizeof(name_buf), "S:%s", name);
    d.SetCursor(layout.x, layout.y_body);
    d.WriteString(name_buf, Font_6x8, true);

    constexpr int kWaveX = 0;
    constexpr int kWaveY = 16;
    constexpr int kWaveW = 128;
    constexpr int kWaveH = 16;
    DrawWaveformPreview(d, sample, edit, kWaveX, kWaveY, kWaveW, kWaveH);

    char tune_buf[12];
    char gain_buf[12];
    FormatSignedInt(app.engine_tune_semitones[layer], tune_buf, sizeof(tune_buf));
    FormatDb(app.engine_gain_db[layer], gain_buf, sizeof(gain_buf));

    char line[32];
    const uint8_t row = app.perform_engine_row % static_cast<uint8_t>(kEngineRowCount);

    std::snprintf(line, sizeof(line), "%c LOAD", (row == kEngineRowLoad) ? '>' : ' ');
    d.SetCursor(0, 32);
    d.WriteString(line, Font_6x8, true);

    std::snprintf(line, sizeof(line), "%c TUNE:%s", (row == kEngineRowTune) ? '>' : ' ', tune_buf);
    d.SetCursor(0, 40);
    d.WriteString(line, Font_6x8, true);

    std::snprintf(line, sizeof(line), "%c GAIN:%s", (row == kEngineRowGain) ? '>' : ' ', gain_buf);
    d.SetCursor(0, 48);
    d.WriteString(line, Font_6x8, true);

    std::snprintf(line,
                  sizeof(line),
                  "%c MODE:%s",
                  (row == kEngineRowOneShot) ? '>' : ' ',
                  PlayModeName(app.engine_play_mode[layer]));
    d.SetCursor(0, 56);
    d.WriteString(line, Font_6x8, true);
}

static bool PerformPlaceholder_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    (void)ctx;
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
        return true;
    return false;
}

static void PerformPlaceholder_Render(UiScreenCtx& ctx, const char* title)
{
    if(!ctx.app || !ctx.display)
        return;

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(*ctx.app, status, sizeof(status));
    UiDraw_Header(d, layout, title, status);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    d.WriteString("TODO", Font_6x8, true);
    UiDraw_Footer(d, layout, "P2:BACK");
}

static void PerformKeyzone_Render(UiScreenCtx& ctx)
{
    PerformPlaceholder_Render(ctx, "KEYZONE");
}

static void PerformAdsr_Render(UiScreenCtx& ctx)
{
    PerformPlaceholder_Render(ctx, "ADSR");
}

static void PerformEmphasis_Render(UiScreenCtx& ctx)
{
    PerformPlaceholder_Render(ctx, "EMPHASIS");
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

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt)
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

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt)
    {
        if(UiListMenu_OnEnc(sd.menu, e.value))
        {
            ctx.app->ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::BtnDown && (e.id == kUiBtnPod1 || e.id == kUiBtnExtEnc))
    {
        if(sd.wav_count > 0 && !sd.scan_in_progress)
        {
            const uint16_t idx = sd.menu.cursor;
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
    const bool show_status = (sd.status[0] != '\0');
    uint8_t lines_used = 1 + (show_status ? 1 : 0);
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

    const char* sd_ok = sd.sd_ok ? "OK" : "ER";
    uint32_t wavs = sd.wav_count;
    if(wavs > 99u)
        wavs = 99u;
    const uint32_t ld = sd.load_in_progress ? sd.load_progress : 0;
    uint32_t gen = ctx.app->sd_published_gen.load(std::memory_order_relaxed);
    if(gen > 99u)
        gen %= 100u;
    uint8_t cur_slot = ctx.app->sd_current_slot.load(std::memory_order_relaxed);

    char buf[32];
    d.SetCursor(layout.x, layout.y_body);
    if(sd.scan_in_progress)
        std::snprintf(buf, sizeof(buf), "SD:%s SCAN...", sd_ok);
    else
        std::snprintf(buf, sizeof(buf), "SD:%s W:%02lu L:%03lu",
                      sd_ok,
                      (unsigned long)wavs,
                      (unsigned long)ld);
    d.WriteString(buf, Font_6x8, true);

    if(show_status && lines_used > 1)
    {
        d.SetCursor(layout.x, layout.y_body + layout.line_h);
        std::snprintf(buf, sizeof(buf), "MSG:%s", sd.status);
        d.WriteString(buf, Font_6x8, true);
    }
    else if(lines_used > 1)
    {
        d.SetCursor(layout.x, layout.y_body + layout.line_h);
        std::snprintf(buf, sizeof(buf), "GEN:%02lu CUR:%u",
                      (unsigned long)gen,
                      (unsigned)cur_slot);
        d.WriteString(buf, Font_6x8, true);
    }

    UiListMenu_Render(sd.menu,
                      d,
                      layout.x,
                      layout.y_body + layout.line_h * lines_used,
                      layout.line_h);

    UiDraw_Footer(d, layout, "A=LOAD  B=BACK");
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
       && (e.id == kUiBtnExtEnc || e.id == kUiBtnPod1))
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
    static const UiScreen perform_menu{UiScreenId::PerformMenu, nullptr, nullptr, PerformMenu_OnEvent, PerformMenu_Render, PerformMenu_OnEnter};
    static const UiScreen perform_engine{UiScreenId::PerformEngine,
                                         PerformEngine_OnScreenEnter,
                                         nullptr,
                                         PerformEngine_OnEvent,
                                         PerformEngine_Render,
                                         PerformEngine_OnEnter};
    static const UiScreen perform_keyzone{UiScreenId::PerformKeyzone, nullptr, nullptr, PerformPlaceholder_OnEvent, PerformKeyzone_Render};
    static const UiScreen perform_adsr{UiScreenId::PerformAdsr, nullptr, nullptr, PerformPlaceholder_OnEvent, PerformAdsr_Render};
    static const UiScreen perform_emphasis{UiScreenId::PerformEmphasis, nullptr, nullptr, PerformPlaceholder_OnEvent, PerformEmphasis_Render};
    static const UiScreen hud{UiScreenId::Hud, nullptr, nullptr, Hud_OnEvent, Hud_Render};
    static const UiScreen fx{UiScreenId::Fx, nullptr, nullptr, Fx_OnEvent, Fx_Render};
    static const UiScreen mod{UiScreenId::Mod, nullptr, nullptr, Mod_OnEvent, Mod_Render};
    static const UiScreen macro{UiScreenId::Macro, nullptr, nullptr, Macro_OnEvent, Macro_Render};
    static const UiScreen sd{UiScreenId::SdBrowse, SdBrowse_OnEnter, nullptr, SdBrowse_OnEvent, SdBrowse_Render};
    static const UiScreen se{UiScreenId::SampleEdit, nullptr, nullptr, SampleEdit_OnEvent, SampleEdit_Render};

    switch(id)
    {
        case UiScreenId::Start:
            return start;
        case UiScreenId::PerformMenu:
            return perform_menu;
        case UiScreenId::PerformEngine:
            return perform_engine;
        case UiScreenId::PerformKeyzone:
            return perform_keyzone;
        case UiScreenId::PerformAdsr:
            return perform_adsr;
        case UiScreenId::PerformEmphasis:
            return perform_emphasis;
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
        case UiScreenId::SampleEdit:
            return se;
        default:
            return hud;
    }
}

void UiRouter_DispatchEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return;

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        if(UiNav_Pop(ctx.app->ui_nav))
            ctx.app->ui_dirty = true;
        return;
    }

    const UiScreen& s = GetScreen(UiNav_Active(ctx.app->ui_nav));
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










