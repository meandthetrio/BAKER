#include "ui_screens.h"
#include "ui_screens_internal.h"
#include "project_actions.h"

#include "app_state.h"
#include "keygroups.h"
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
#include "tilt_eq.h"

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

static float ClampEqTiltDb(float x)
{
    if(x < -kTiltEqTiltMaxDb)
        return -kTiltEqTiltMaxDb;
    if(x > kTiltEqTiltMaxDb)
        return kTiltEqTiltMaxDb;
    return x;
}

static float ClampEqQ(float x)
{
    if(x < kTiltEqQMin)
        return kTiltEqQMin;
    if(x > kTiltEqQMax)
        return kTiltEqQMax;
    return x;
}

static float UiAccelFromDtMs(uint32_t dt_ms)
{
    if(dt_ms <= 20u) return 8.0f;
    if(dt_ms <= 40u) return 5.0f;
    if(dt_ms <= 70u) return 3.0f;
    if(dt_ms <= 120u) return 2.0f;
    return 1.0f;
}

static float UiDeltaNormAccelerated(int enc_delta, uint32_t t_ms, uint32_t& last_t_ms, float base_step)
{
    const uint32_t dt_ms = (last_t_ms == 0u) ? 999u : (t_ms - last_t_ms);
    last_t_ms = t_ms;
    return static_cast<float>(enc_delta) * base_step * UiAccelFromDtMs(dt_ms);
}

static float AdsrFltFaderFromCutoffHz(float hz)
{
    if(hz < 20.0f) hz = 20.0f;
    if(hz > 20000.0f) hz = 20000.0f;
    const float shaped = std::log(hz / 20.0f) / std::log(20000.0f / 20.0f);
    const float v = shaped * shaped;
    return Clamp01(v);
}

static float AdsrFltCutoffHzFromFader(float value)
{
    value = Clamp01(value);
    const float shaped = std::sqrt(value);
    return 20.0f * std::pow(20000.0f / 20.0f, shaped);
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

void DrawScaledText6x8(OledPager& d, const char* text, int x, int y, int scale)
{
    if(!text || scale <= 0)
        return;

    for(int i = 0; text[i] != '\0'; ++i)
    {
        const char ch = text[i];
        if(ch < 32 || ch > 126)
            continue;

        const uint32_t base = static_cast<uint32_t>(ch - 32) * Font_6x8.FontHeight;
        for(uint32_t row = 0; row < Font_6x8.FontHeight; ++row)
        {
            const uint32_t bits = Font_6x8.data[base + row];
            for(uint32_t col = 0; col < Font_6x8.FontWidth; ++col)
            {
                if(((bits << col) & 0x8000u) == 0u)
                    continue;

                const int px = x + i * static_cast<int>(Font_6x8.FontWidth * scale)
                               + static_cast<int>(col * scale);
                const int py = y + static_cast<int>(row * scale);
                for(int dy = 0; dy < scale; ++dy)
                {
                    for(int dx = 0; dx < scale; ++dx)
                        d.DrawPixel(px + dx, py + dy, true);
                }
            }
        }
    }
}

struct Font5x7
{
    static constexpr int W = 5;
    static constexpr int H = 7;

    static void GetGlyphRows(char c, uint8_t out_rows[H])
    {
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

        // Lowercase a-z (real lowercase forms for 5x7 rendering).
        switch(c)
        {
            case 'a': set({0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b10011, 0b01101}); return;
            case 'b': set({0b10000, 0b10000, 0b10110, 0b11001, 0b10001, 0b11001, 0b10110}); return;
            case 'c': set({0b00000, 0b01110, 0b10001, 0b10000, 0b10000, 0b10001, 0b01110}); return;
            case 'd': set({0b00001, 0b00001, 0b01101, 0b10011, 0b10001, 0b10011, 0b01101}); return;
            case 'e': set({0b00000, 0b01110, 0b10001, 0b11111, 0b10000, 0b10001, 0b01110}); return;
            case 'f': set({0b00110, 0b01001, 0b01000, 0b11100, 0b01000, 0b01000, 0b01000}); return;
            case 'g': set({0b00000, 0b01101, 0b10011, 0b10011, 0b01101, 0b00001, 0b01110}); return;
            case 'h': set({0b10000, 0b10000, 0b10110, 0b11001, 0b10001, 0b10001, 0b10001}); return;
            case 'i': set({0b00100, 0b00000, 0b01100, 0b00100, 0b00100, 0b00100, 0b01110}); return;
            case 'j': set({0b00010, 0b00000, 0b00110, 0b00010, 0b00010, 0b10010, 0b01100}); return;
            case 'k': set({0b10000, 0b10001, 0b10010, 0b11100, 0b10010, 0b10001, 0b10001}); return;
            case 'l': set({0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}); return;
            case 'm': set({0b00000, 0b11010, 0b10101, 0b10101, 0b10101, 0b10101, 0b10101}); return;
            case 'n': set({0b00000, 0b10110, 0b11001, 0b10001, 0b10001, 0b10001, 0b10001}); return;
            case 'o': set({0b00000, 0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}); return;
            case 'p': set({0b00000, 0b10110, 0b11001, 0b11001, 0b10110, 0b10000, 0b10000}); return;
            case 'q': set({0b00000, 0b01101, 0b10011, 0b10011, 0b01101, 0b00001, 0b00001}); return;
            case 'r': set({0b00000, 0b10110, 0b11001, 0b10000, 0b10000, 0b10000, 0b10000}); return;
            case 's': set({0b00000, 0b01111, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}); return;
            case 't': set({0b01000, 0b01000, 0b11100, 0b01000, 0b01000, 0b01001, 0b00110}); return;
            case 'u': set({0b00000, 0b10001, 0b10001, 0b10001, 0b10011, 0b10101, 0b01001}); return;
            case 'v': set({0b00000, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}); return;
            case 'w': set({0b00000, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010}); return;
            case 'x': set({0b00000, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}); return;
            case 'y': set({0b00000, 0b10001, 0b10001, 0b10011, 0b01101, 0b00001, 0b01110}); return;
            case 'z': set({0b00000, 0b11111, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}); return;
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
            case '+': set({0, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0}); return;
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

void DrawTinyString(OledPager& d, const char* str, int x, int y, bool on)
{
    const int char_w = Font5x7::W + 1;
    for(int i = 0; str[i] != '\0'; ++i)
    {
        char ch = str[i];
        if(ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');

        uint8_t rows[Font5x7::H] = {};
        Font5x7::GetGlyphRows(ch, rows);
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

static void DrawTinyStringCaseSensitive(OledPager& d, const char* str, int x, int y, bool on)
{
    const int char_w = Font5x7::W + 1;
    int pen_x = x;
    for(int i = 0; str[i] != '\0'; ++i)
    {
        const char ch = str[i];

        if(ch == 'H' || ch == 'L' || ch == '#')
        {
            if(ch == '#')
            {
                static constexpr uint8_t kSharpRows[Font5x7::H] = {
                    0b01010,
                    0b11111,
                    0b01010,
                    0b11111,
                    0b01010,
                    0b01010,
                    0b00000,
                };
                for(int yy = 0; yy < Font5x7::H; ++yy)
                {
                    const uint8_t row = kSharpRows[yy];
                    for(int xx = 0; xx < Font5x7::W; ++xx)
                    {
                        if((row >> (Font5x7::W - 1 - xx)) & 1u)
                        {
                            const int px = pen_x + xx;
                            const int py = y + yy;
                            if(px >= 0 && px < 128 && py >= 0 && py < 64)
                                d.DrawPixel(px, py, on);
                        }
                    }
                }
            }
            else
            {
                static constexpr int kTallUpperH = 9;
                static constexpr uint8_t kTallHRows[kTallUpperH] = {
                    0b10001,
                    0b10001,
                    0b10001,
                    0b10001,
                    0b11111,
                    0b10001,
                    0b10001,
                    0b10001,
                    0b10001,
                };
                static constexpr uint8_t kTallLRows[kTallUpperH] = {
                    0b10000,
                    0b10000,
                    0b10000,
                    0b10000,
                    0b10000,
                    0b10000,
                    0b10000,
                    0b10000,
                    0b11111,
                };
                const uint8_t* rows = (ch == 'H') ? kTallHRows : kTallLRows;
                const int glyph_y = y - 2;

                for(int yy = 0; yy < kTallUpperH; ++yy)
                {
                    const uint8_t row = rows[yy];
                    for(int xx = 0; xx < Font5x7::W; ++xx)
                    {
                        if((row >> (Font5x7::W - 1 - xx)) & 1u)
                        {
                            const int px = pen_x + xx;
                            const int py = glyph_y + yy;
                            if(px >= 0 && px < 128 && py >= 0 && py < 64)
                                d.DrawPixel(px, py, on);
                        }
                    }
                }
            }

            int advance = char_w;
            if(ch == ':')
                advance -= 1;
            else if(str[i + 1] == ':')
                advance -= 1;
            pen_x += advance;
            continue;
        }

        uint8_t rows[Font5x7::H] = {};
        Font5x7::GetGlyphRows(ch, rows);
        for(int yy = 0; yy < Font5x7::H; ++yy)
        {
            const uint8_t row = rows[yy];
            for(int xx = 0; xx < Font5x7::W; ++xx)
            {
                if((row >> (Font5x7::W - 1 - xx)) & 1u)
                {
                    const int px = pen_x + xx;
                    const int py = y + yy;
                    if(px >= 0 && px < 128 && py >= 0 && py < 64)
                        d.DrawPixel(px, py, on);
                }
            }
        }

        int advance = char_w;
        if(ch == ':')
            advance -= 1;
        else if(str[i + 1] == ':')
            advance -= 1;
        pen_x += advance;
    }
}

static int TinyStringWidthCaseSensitiveTightColons(const char* str)
{
    if(str == nullptr || str[0] == '\0')
        return 0;

    const int char_w = Font5x7::W + 1;
    int width = 0;
    for(int i = 0; str[i] != '\0'; ++i)
    {
        int advance = char_w;
        if(str[i] == ':')
            advance -= 1;
        else if(str[i + 1] == ':')
            advance -= 1;
        width += advance;
    }
    return width - 1;
}

int TinyStringWidth(const char* str)
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

static constexpr int kMini3x5W = 3;
static constexpr int kMini3x5H = 5;
static constexpr int kMini3x5Advance = kMini3x5W + 1;

static void GetMini3x5Glyph(char c, uint8_t out_rows[kMini3x5H])
{
    for(int i = 0; i < kMini3x5H; ++i)
        out_rows[i] = 0;

    if(c >= 'A' && c <= 'Z')
        c = static_cast<char>(c - 'A' + 'a');

    switch(c)
    {
        case '0': out_rows[0] = 0b111; out_rows[1] = 0b101; out_rows[2] = 0b101; out_rows[3] = 0b101; out_rows[4] = 0b111; return;
        case '1': out_rows[0] = 0b010; out_rows[1] = 0b110; out_rows[2] = 0b010; out_rows[3] = 0b010; out_rows[4] = 0b111; return;
        case '2': out_rows[0] = 0b111; out_rows[1] = 0b001; out_rows[2] = 0b111; out_rows[3] = 0b100; out_rows[4] = 0b111; return;
        case '3': out_rows[0] = 0b111; out_rows[1] = 0b001; out_rows[2] = 0b111; out_rows[3] = 0b001; out_rows[4] = 0b111; return;
        case '4': out_rows[0] = 0b101; out_rows[1] = 0b101; out_rows[2] = 0b111; out_rows[3] = 0b001; out_rows[4] = 0b001; return;
        case '5': out_rows[0] = 0b111; out_rows[1] = 0b100; out_rows[2] = 0b111; out_rows[3] = 0b001; out_rows[4] = 0b111; return;
        case '6': out_rows[0] = 0b111; out_rows[1] = 0b100; out_rows[2] = 0b111; out_rows[3] = 0b101; out_rows[4] = 0b111; return;
        case '7': out_rows[0] = 0b111; out_rows[1] = 0b001; out_rows[2] = 0b001; out_rows[3] = 0b001; out_rows[4] = 0b001; return;
        case '8': out_rows[0] = 0b111; out_rows[1] = 0b101; out_rows[2] = 0b111; out_rows[3] = 0b101; out_rows[4] = 0b111; return;
        case '9': out_rows[0] = 0b111; out_rows[1] = 0b101; out_rows[2] = 0b111; out_rows[3] = 0b001; out_rows[4] = 0b111; return;
        case 'a': out_rows[0] = 0b010; out_rows[1] = 0b001; out_rows[2] = 0b011; out_rows[3] = 0b101; out_rows[4] = 0b111; return;
        case 'd': out_rows[0] = 0b001; out_rows[1] = 0b001; out_rows[2] = 0b011; out_rows[3] = 0b101; out_rows[4] = 0b011; return;
        case 'e': out_rows[0] = 0b111; out_rows[1] = 0b100; out_rows[2] = 0b110; out_rows[3] = 0b100; out_rows[4] = 0b111; return;
        case 'h': out_rows[0] = 0b100; out_rows[1] = 0b100; out_rows[2] = 0b110; out_rows[3] = 0b101; out_rows[4] = 0b101; return;
        case 'l': out_rows[0] = 0b110; out_rows[1] = 0b010; out_rows[2] = 0b010; out_rows[3] = 0b010; out_rows[4] = 0b111; return;
        case 'n': out_rows[0] = 0b110; out_rows[1] = 0b101; out_rows[2] = 0b101; out_rows[3] = 0b101; out_rows[4] = 0b101; return;
        case 'o': out_rows[0] = 0b010; out_rows[1] = 0b101; out_rows[2] = 0b101; out_rows[3] = 0b101; out_rows[4] = 0b010; return;
        case 'p': out_rows[0] = 0b110; out_rows[1] = 0b101; out_rows[2] = 0b110; out_rows[3] = 0b100; out_rows[4] = 0b100; return;
        case 'r': out_rows[0] = 0b110; out_rows[1] = 0b101; out_rows[2] = 0b100; out_rows[3] = 0b100; out_rows[4] = 0b100; return;
        case 's': out_rows[0] = 0b011; out_rows[1] = 0b100; out_rows[2] = 0b010; out_rows[3] = 0b001; out_rows[4] = 0b110; return;
        case 't': out_rows[0] = 0b111; out_rows[1] = 0b010; out_rows[2] = 0b010; out_rows[3] = 0b010; out_rows[4] = 0b010; return;
        case ' ': return;
        default: return;
    }
}

static void DrawMiniString3x5(OledPager& d, const char* str, int x, int y, bool on)
{
    if(str == nullptr)
        return;

    for(int i = 0; str[i] != '\0'; ++i)
    {
        uint8_t rows[kMini3x5H] = {};
        GetMini3x5Glyph(str[i], rows);
        for(int yy = 0; yy < kMini3x5H; ++yy)
        {
            const uint8_t row = rows[yy];
            for(int xx = 0; xx < kMini3x5W; ++xx)
            {
                if((row >> (kMini3x5W - 1 - xx)) & 1u)
                {
                    const int px = x + i * kMini3x5Advance + xx;
                    const int py = y + yy;
                    if(px >= 0 && px < 128 && py >= 0 && py < 64)
                        d.DrawPixel(px, py, on);
                }
            }
        }
    }
}

static int MiniString3x5Width(const char* str)
{
    if(str == nullptr || str[0] == '\0')
        return 0;
    int count = 0;
    for(; str[count] != '\0'; ++count)
    {
    }
    return count * kMini3x5Advance - 1;
}

static void DrawPlus5x7(OledPager& d, int x, int y, bool on)
{
    // 5x7 plus centered in the same cell width used by DrawTinyString.
    for(int yy = 1; yy <= 5; ++yy)
        d.DrawPixel(x + 2, y + yy, on);
    for(int xx = 0; xx < 5; ++xx)
        d.DrawPixel(x + xx, y + 3, on);
}

static int SignedSemitoneTextWidth(int v)
{
    if(v > 0)
    {
        char digits[8];
        std::snprintf(digits, sizeof(digits), "%d", v);
        return 6 + TinyStringWidth(digits); // '+' cell (6px advance) + digits
    }

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return TinyStringWidth(buf);
}

static void DrawSignedSemitoneText(OledPager& d, int v, int x, int y, bool on)
{
    if(v > 0)
    {
        char digits[8];
        std::snprintf(digits, sizeof(digits), "%d", v);
        DrawPlus5x7(d, x, y, on);
        DrawTinyString(d, digits, x + 6, y, on);
        return;
    }

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", v);
    DrawTinyString(d, buf, x, y, on);
}

static void DrawDottedRect(OledPager& d, int x0, int y0, int x1, int y1, bool on)
{
    if(x1 < x0)
    {
        const int t = x0;
        x0 = x1;
        x1 = t;
    }
    if(y1 < y0)
    {
        const int t = y0;
        y0 = y1;
        y1 = t;
    }

    for(int x = x0; x <= x1; ++x)
    {
        if(((x - x0) & 1) == 0)
        {
            d.DrawPixel(x, y0, on);
            d.DrawPixel(x, y1, on);
        }
    }
    for(int y = y0; y <= y1; ++y)
    {
        if(((y - y0) & 1) == 0)
        {
            d.DrawPixel(x0, y, on);
            d.DrawPixel(x1, y, on);
        }
    }
}

static int LoadWordmarkWidth()
{
    // 5 + 1 + 3 + 1 + 3 + 1 + 4
    return 18;
}

static void DrawLoadWordmark(OledPager& d, int x, int y, bool on)
{
    constexpr int l_x = 0;   // width 5 (0..4)
    constexpr int o_x = 6;   // width 3 (6..8), 1px gap after L
    constexpr int a_x = 10;  // width 3 (10..12), 1px gap after o
    constexpr int d_x = 14;  // width 4 (14..17), 1px gap after a

    auto draw_char_3x5 = [&](char ch, int cx, int cy)
    {
        uint8_t rows[5] = {};
        switch(ch)
        {
            case 'o':
                rows[0] = 0b010;
                rows[1] = 0b101;
                rows[2] = 0b101;
                rows[3] = 0b101;
                rows[4] = 0b010;
                break;
            case 'a':
                rows[0] = 0b010;
                rows[1] = 0b001;
                rows[2] = 0b011;
                rows[3] = 0b101;
                rows[4] = 0b111;
                break;
            default:
                return;
        }

        for(int yy = 0; yy < 5; ++yy)
        {
            const uint8_t row = rows[yy];
            for(int xx = 0; xx < 3; ++xx)
            {
                if((row >> (2 - xx)) & 1)
                {
                    const int px = cx + xx;
                    const int py = cy + yy;
                    if(px >= 0 && px < 128 && py >= 0 && py < 64)
                        d.DrawPixel(px, py, on);
                }
            }
        }
    };

    auto draw_l_top_extended = [&](int cx, int cy)
    {
        // Extend only the top of the vertical stroke by one pixel.
        for(int yy = -1; yy <= 6; ++yy)
        {
            const int py = cy + yy;
            if(cx >= 0 && cx < 128 && py >= 0 && py < 64)
                d.DrawPixel(cx, py, on);
        }
        const int foot_y = cy + 6;
        if(foot_y >= 0 && foot_y < 64)
        {
            for(int xx = 0; xx < 5; ++xx)
            {
                const int px = cx + xx;
                if(px >= 0 && px < 128)
                    d.DrawPixel(px, foot_y, on);
            }
        }
    };

    auto draw_D_4x7 = [&](int cx, int cy)
    {
        const uint8_t rows[7] = {
            0b1110,
            0b1001,
            0b1001,
            0b1001,
            0b1001,
            0b1001,
            0b1110,
        };
        for(int yy = 0; yy < 7; ++yy)
        {
            for(int xx = 0; xx < 4; ++xx)
            {
                if((rows[yy] >> (3 - xx)) & 1)
                {
                    const int px = cx + xx;
                    const int py = cy + yy;
                    if(px >= 0 && px < 128 && py >= 0 && py < 64)
                        d.DrawPixel(px, py, on);
                }
            }
        }
    };

    draw_l_top_extended(x + l_x, y);
    draw_char_3x5('o', x + o_x, y);
    draw_char_3x5('a', x + a_x, y);
    draw_D_4x7(x + d_x, y);

    // Extended baseline from L to one pixel before D.
    const int baseline_y = y + Font5x7::H - 1;
    const int baseline_end_x = x + d_x - 2; // leaves one blank pixel before D
    d.DrawLine(x, baseline_y, baseline_end_x, baseline_y, on);
}

static int TuneWordmarkWidth()
{
    // 5 + 1 + 3 + 1 + 3 + 1 + 3
    return 17;
}

static void DrawTuneWordmark(OledPager& d, int x, int y, bool on)
{
    constexpr int t_x = 0;   // width 5 (0..4)
    constexpr int u_x = 6;   // width 3 (6..8), 1px gap after T
    constexpr int n_x = 10;  // width 3 (10..12), 1px gap after u
    constexpr int e_x = 14;  // width 3 (14..16), 1px gap after n

    auto draw_char_3x5 = [&](char ch, int cx, int cy)
    {
        uint8_t rows[5] = {};
        switch(ch)
        {
            case 'u':
                rows[0] = 0b101;
                rows[1] = 0b101;
                rows[2] = 0b101;
                rows[3] = 0b101;
                rows[4] = 0b011;
                break;
            case 'n':
                rows[0] = 0b110;
                rows[1] = 0b101;
                rows[2] = 0b101;
                rows[3] = 0b101;
                rows[4] = 0b101;
                break;
            case 'e':
                rows[0] = 0b111;
                rows[1] = 0b100;
                rows[2] = 0b110;
                rows[3] = 0b100;
                rows[4] = 0b111;
                break;
            default:
                return;
        }

        for(int yy = 0; yy < 5; ++yy)
        {
            const uint8_t row = rows[yy];
            for(int xx = 0; xx < 3; ++xx)
            {
                if((row >> (2 - xx)) & 1)
                {
                    const int px = cx + xx;
                    const int py = cy + yy;
                    if(px >= 0 && px < 128 && py >= 0 && py < 64)
                        d.DrawPixel(px, py, on);
                }
            }
        }
    };

    auto draw_t = [&](int cx, int cy)
    {
        // T top bar.
        for(int xx = 0; xx < 5; ++xx)
        {
            const int px = cx + xx;
            if(px >= 0 && px < 128 && cy >= 0 && cy < 64)
                d.DrawPixel(px, cy, on);
        }
        // T vertical stem.
        for(int yy = 1; yy < 7; ++yy)
        {
            const int py = cy + yy;
            const int px = cx + 2;
            if(px >= 0 && px < 128 && py >= 0 && py < 64)
                d.DrawPixel(px, py, on);
        }
    };

    draw_t(x + t_x, y);
    draw_char_3x5('u', x + u_x, y + 2);
    draw_char_3x5('n', x + n_x, y + 2);
    draw_char_3x5('e', x + e_x, y + 2);

    // Extended top line from T across u/n/e.
    d.DrawLine(x, y, x + TuneWordmarkWidth() - 1, y, on);
}

static constexpr int kMicroW = 4;
static constexpr int kMicroH = 6;
static constexpr int kMicroAdvance = kMicroW + 1;

static void GetMicroGlyph(char c, uint8_t out_rows[kMicroH])
{
    for(int i = 0; i < kMicroH; ++i)
        out_rows[i] = 0;

    auto set = [&](uint8_t r0, uint8_t r1, uint8_t r2, uint8_t r3, uint8_t r4, uint8_t r5)
    {
        out_rows[0] = r0;
        out_rows[1] = r1;
        out_rows[2] = r2;
        out_rows[3] = r3;
        out_rows[4] = r4;
        out_rows[5] = r5;
    };

    // Native lowercase forms so Title Case looks correct.
    switch(c)
    {
        case 'a': set(0b0000, 0b0110, 0b0001, 0b0111, 0b1001, 0b0111); return;
        case 'b': set(0b1000, 0b1000, 0b1110, 0b1001, 0b1001, 0b1110); return;
        case 'c': set(0b0000, 0b0110, 0b1001, 0b1000, 0b1001, 0b0110); return;
        case 'd': set(0b0001, 0b0001, 0b0111, 0b1001, 0b1001, 0b0111); return;
        case 'e': set(0b0000, 0b0110, 0b1001, 0b1111, 0b1000, 0b0111); return;
        case 'f': set(0b0011, 0b0100, 0b1110, 0b0100, 0b0100, 0b0100); return;
        case 'g': set(0b0000, 0b0111, 0b1001, 0b0111, 0b0001, 0b1110); return;
        case 'h': set(0b1000, 0b1000, 0b1110, 0b1001, 0b1001, 0b1001); return;
        case 'i': set(0b0010, 0b0000, 0b0110, 0b0010, 0b0010, 0b0111); return;
        case 'j': set(0b0001, 0b0000, 0b0011, 0b0001, 0b1001, 0b0110); return;
        case 'k': set(0b1000, 0b1001, 0b1010, 0b1100, 0b1010, 0b1001); return;
        case 'l': set(0b0110, 0b0010, 0b0010, 0b0010, 0b0010, 0b0111); return;
        case 'm': set(0b0000, 0b1110, 0b1011, 0b1011, 0b1011, 0b1011); return;
        case 'n': set(0b0000, 0b1110, 0b1001, 0b1001, 0b1001, 0b1001); return;
        case 'o': set(0b0000, 0b0110, 0b1001, 0b1001, 0b1001, 0b0110); return;
        case 'p': set(0b0000, 0b1110, 0b1001, 0b1110, 0b1000, 0b1000); return;
        case 'q': set(0b0000, 0b0111, 0b1001, 0b0111, 0b0001, 0b0001); return;
        case 'r': set(0b0000, 0b1011, 0b1100, 0b1000, 0b1000, 0b1000); return;
        case 's': set(0b0000, 0b0111, 0b1000, 0b0110, 0b0001, 0b1110); return;
        case 't': set(0b0100, 0b1110, 0b0100, 0b0100, 0b0101, 0b0010); return;
        case 'u': set(0b0000, 0b1001, 0b1001, 0b1001, 0b1011, 0b0101); return;
        case 'v': set(0b0000, 0b1001, 0b1001, 0b1001, 0b0101, 0b0010); return;
        case 'w': set(0b0000, 0b1001, 0b1001, 0b1011, 0b1011, 0b0110); return;
        case 'x': set(0b0000, 0b1001, 0b0110, 0b0110, 0b0110, 0b1001); return;
        case 'y': set(0b0000, 0b1001, 0b1001, 0b0111, 0b0001, 0b1110); return;
        case 'z': set(0b0000, 0b1111, 0b0010, 0b0100, 0b1000, 0b1111); return;
        default: break;
    }

    uint8_t rows5[Font5x7::H] = {};
    Font5x7::GetGlyphRows(c, rows5);

    // Convert 5x7 -> 4x6 by dropping one column and one row.
    constexpr int col_map[kMicroW] = {0, 1, 3, 4};
    for(int yy = 0; yy < kMicroH; ++yy)
    {
        uint8_t bits = 0;
        const uint8_t src = rows5[yy];
        for(int xx = 0; xx < kMicroW; ++xx)
        {
            const int src_col = col_map[xx];
            const bool on = ((src >> (Font5x7::W - 1 - src_col)) & 1u) != 0;
            if(on)
                bits |= static_cast<uint8_t>(1u << (kMicroW - 1 - xx));
        }
        out_rows[yy] = bits;
    }
}

static void DrawMicroString(OledPager& d, const char* str, int x, int y, bool on)
{
    if(str == nullptr)
        return;

    for(int i = 0; str[i] != '\0'; ++i)
    {
        if(str[i] == 'm')
        {
            // Let lowercase 'm' use the existing 1px tracking gap so its right inner void stays visible.
            static constexpr uint8_t kMicroMRows[kMicroH] = {
                0b00000,
                0b11110,
                0b10101,
                0b10101,
                0b10101,
                0b10101,
            };
            for(int yy = 0; yy < kMicroH; ++yy)
            {
                const uint8_t row = kMicroMRows[yy];
                for(int xx = 0; xx < 5; ++xx)
                {
                    if((row >> (4 - xx)) & 1u)
                    {
                        const int px = x + i * kMicroAdvance + xx;
                        const int py = y + yy;
                        if(px >= 0 && px < 128 && py >= 0 && py < 64)
                            d.DrawPixel(px, py, on);
                    }
                }
            }
            continue;
        }

        uint8_t rows[kMicroH] = {};
        GetMicroGlyph(str[i], rows);
        for(int yy = 0; yy < kMicroH; ++yy)
        {
            const uint8_t row = rows[yy];
            for(int xx = 0; xx < kMicroW; ++xx)
            {
                if((row >> (kMicroW - 1 - xx)) & 1u)
                {
                    const int px = x + i * kMicroAdvance + xx;
                    const int py = y + yy;
                    if(px >= 0 && px < 128 && py >= 0 && py < 64)
                        d.DrawPixel(px, py, on);
                }
            }
        }
    }
}

static int MicroStringWidth(const char* str)
{
    if(str == nullptr || str[0] == '\0')
        return 0;
    int count = 0;
    for(; str[count] != '\0'; ++count)
    {
    }
    return count * kMicroAdvance - 1;
}

static void ToLowerCase(const char* in, char* out, size_t out_sz)
{
    if(out_sz == 0)
        return;
    out[0] = '\0';
    if(!in)
        return;

    size_t j = 0;
    for(size_t i = 0; in[i] != '\0' && j + 1 < out_sz; ++i)
    {
        char c = in[i];
        if(c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        out[j++] = c;
    }
    out[j] = '\0';
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
                                     const bool* hide_handles = nullptr,
                                     int selected_label_box_y_offset = 0,
                                     int selected_label_box_extra_bottom = 0,
                                     int selected_label_box_bottom_clip_extra = 0)
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
                int ly0 = label_y - 1 + selected_label_box_y_offset;
                int ly1 = label_y + Font5x7::H + selected_label_box_y_offset
                          + selected_label_box_extra_bottom;
                int ly1_max = y + h - 2 + selected_label_box_bottom_clip_extra;
                if(ly1_max > y + h - 1)
                    ly1_max = y + h - 1;
                if(lx0 < x + 1) lx0 = x + 1;
                if(lx1 > x + w - 2) lx1 = x + w - 2;
                if(ly0 < y + 1) ly0 = y + 1;
                if(ly1 > ly1_max) ly1 = ly1_max;
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

// Clockwise perimeter (inclusive rect), then sparse dots with phase advancing ~1 step / 200ms.
static void DrawClockwiseMarchingDottedRect(OledPager& d, int x0, int y0, int x1, int y1, uint32_t now_ms)
{
    if(x1 < x0)
    {
        const int t = x0;
        x0 = x1;
        x1 = t;
    }
    if(y1 < y0)
    {
        const int t = y0;
        y0 = y1;
        y1 = t;
    }
    if(x1 - x0 < 1 || y1 - y0 < 1)
        return;

    int px[320];
    int py[320];
    int n = 0;
    auto push = [&](int x, int y)
    {
        if(n < static_cast<int>(sizeof(px) / sizeof(px[0])))
        {
            px[n] = x;
            py[n] = y;
            ++n;
        }
    };
    for(int x = x0; x <= x1; ++x)
        push(x, y0);
    for(int y = y0 + 1; y <= y1; ++y)
        push(x1, y);
    for(int x = x1 - 1; x >= x0; --x)
        push(x, y1);
    for(int y = y1 - 1; y > y0; --y)
        push(x0, y);
    if(n <= 0)
        return;
    const int step = static_cast<int>(now_ms / 200u);
    for(int i = 0; i < n; ++i)
    {
        if(((i + step) % 3) == 0)
            d.DrawPixel(px[i], py[i], true);
    }
}

// DELAY detail: vertical letter stacks (LTM/RTM/FBK/MIX), each glyph centered in the label column.
static void DrawDelayDetailFaders(OledPager& d,
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

    constexpr int kCharColW   = 6;
    constexpr int kLabelGap   = 2;
    constexpr int kShiftLtmRtmFbkPx = 5;
    constexpr int kMixLabelLeftPx   = 3;
    constexpr int kDisplayH         = 64;
    const int       line_top    = y + 2;
    const int       line_bottom = y + h - 2;
    if(line_bottom <= line_top)
        return;

    int fader_left = x + 4 + kCharColW + kLabelGap;
    int fader_right = x + w - 5;
    if(fader_right <= fader_left)
        return;

    const int span_x = fader_right - fader_left;
    const int span_y = line_bottom - line_top;
    constexpr int kCount = 4;

    for(int f = 0; f < kCount; ++f)
    {
        int line_x = fader_left;
        if(kCount > 1 && span_x > 0)
            line_x = fader_left + (span_x * f) / (kCount - 1);
        // LTM/RTM/FBK: rails shift right; labels stay on the unshifted column grid.
        const int rail_x_center = line_x + ((f <= 2) ? kShiftLtmRtmFbkPx : 0);

        int col_right = line_x - kLabelGap;
        int col_left  = col_right - kCharColW + 1;
        if(f == 3)
        {
            col_left -= kMixLabelLeftPx;
            col_right -= kMixLabelLeftPx;
        }
        const char* lab     = labels[f];
        const int   lab_len = (lab != nullptr) ? static_cast<int>(std::strlen(lab)) : 0;
        if(lab != nullptr && lab_len > 0)
        {
            constexpr int kMaxLabelStack = 3;
            const int     nch            = lab_len < kMaxLabelStack ? lab_len : kMaxLabelStack;
            constexpr int kStackGap      = 1;
            const int     stack_h        = nch * Font5x7::H + (nch > 1 ? (nch - 1) * kStackGap : 0);
            const int     y_start        = line_top + (span_y - stack_h) / 2;
            if(y_start >= line_top && nch > 0)
            {
                out_lbl_x0[f] = col_left - 1;
                out_lbl_y0[f] = y_start - 1;
                out_lbl_x1[f] = col_right;
                out_lbl_y1[f] = y_start + stack_h;

                const bool inv_fbk_mix = select_active && f == selected_index && f >= 2;
                if(inv_fbk_mix)
                {
                    d.DrawRect(out_lbl_x0[f], out_lbl_y0[f], out_lbl_x1[f], out_lbl_y1[f], true, true);
                }
                for(int c = 0; c < nch; ++c)
                {
                    char      one[2] = {lab[c], '\0'};
                    const int cw     = TinyStringWidth(one);
                    int       cx     = col_left + (kCharColW - cw) / 2;
                    if(cx < x + 1)
                        cx = x + 1;
                    const int cy = y_start + c * (Font5x7::H + kStackGap);
                    DrawTinyString(d, one, cx, cy, !inv_fbk_mix);
                }
            }
        }

        int rail_x = rail_x_center;
        if(rail_x < x + 1)
            rail_x = x + 1;
        if(rail_x > x + w - 2)
            rail_x = x + w - 2;

        const int rail_top    = (f <= 1) ? 0 : line_top;
        const int rail_bottom = (f <= 1) ? (kDisplayH - 1) : line_bottom;
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
                                int h,
                                bool on = true,
                                bool outline_only = false,
                                bool dotted_border = false);

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

bool MainMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
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

bool MainMenu_OnEnter(UiScreenCtx& ctx)
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

void MainMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const int selected = static_cast<int>(ctx.app->main_menu_index % kMainMenuCount);
    DrawMainMenuFriendStyle(*ctx.display, selected);
}



// HOME -> PRESETS (blank placeholder screen for now)
bool Presets_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    (void)ctx;
    (void)e;
    // Do not consume BACK events; let the router/nav handle it.
    return false;
}

void Presets_Render(UiScreenCtx& ctx)
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

bool Record_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
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

void Record_Render(UiScreenCtx& ctx)
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
        if(!app.sd.save_in_progress && !app.worker.ui_req_busy)
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
            DrawScaledText6x8(d, big, text_x, text_y, scale);
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

void Record_OnEnter(UiScreenCtx& ctx)
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

void Record_OnExit(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    AppState& app = *ctx.app;
    Record_StopPreview(app);
    app.rec_monitor_enable.store(0, std::memory_order_release);
    app.record_anim_start_ms = -1.0;
    app.rec_stop_req.store(1, std::memory_order_release);
}

bool PerformMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
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

bool PerformMenu_OnEnter(UiScreenCtx& ctx)
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

void PerformMenu_Render(UiScreenCtx& ctx)
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
static constexpr int32_t kAdsrRowCount = 3;
static constexpr int32_t kAdsrRowOneShot = 0;
static constexpr int32_t kAdsrRowLoop = 1;
static constexpr int32_t kAdsrRowAdsr = 2;
static constexpr int32_t kAdsrStageCount = 4;
static constexpr float kPerformLoopCrossfadeMin = 0.0f;
static constexpr float kPerformLoopCrossfadeMax = 0.5f;
static constexpr float kPerformLoopCrossfadeStep = 1.0f / 128.0f;
static constexpr float kPerformLoopCrossfadeShapeMin = 0.0f;
static constexpr float kPerformLoopCrossfadeShapeMax = 1.0f;
static constexpr float kPerformLoopCrossfadeShapeStep = 1.0f / 64.0f;
static constexpr uint16_t kPerformAdsrAttackReleaseMinMs = 1u;
static constexpr uint16_t kPerformAdsrAttackReleaseMaxMs = 1000u;
static constexpr uint16_t kPerformAdsrDecayMaxMs = 100u;
static constexpr uint16_t kPerformAdsrSustainMax = 100u;

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static float ComputePerformLoopCrossfadeWeight(float mix, float shape, bool fade_in)
{
    mix = Clamp01(mix);
    shape = Clamp01(shape);
    const float linear = fade_in ? mix : (1.0f - mix);
    const float equal_power = fade_in ? std::sqrt(mix) : std::sqrt(1.0f - mix);
    return linear + (equal_power - linear) * shape;
}

static uint16_t PerformAdsrStageValue(const AppState& app, uint8_t layer, uint8_t stage)
{
    const uint8_t safe_layer = layer & 1u;
    switch(stage % static_cast<uint8_t>(kAdsrStageCount))
    {
        case 0: return app.perform_adsr_loop_attack[safe_layer];
        case 1: return app.perform_adsr_loop_decay[safe_layer];
        case 2: return app.perform_adsr_loop_sustain[safe_layer];
        default: return app.perform_adsr_loop_release[safe_layer];
    }
}

static void SetPerformAdsrStageValue(AppState& app, uint8_t layer, uint8_t stage, uint16_t value)
{
    const uint8_t safe_layer = layer & 1u;
    switch(stage % static_cast<uint8_t>(kAdsrStageCount))
    {
        case 0:
            app.perform_adsr_loop_attack[safe_layer] = value;
            return;
        case 1:
            app.perform_adsr_loop_decay[safe_layer] = static_cast<uint8_t>(value);
            return;
        case 2:
            app.perform_adsr_loop_sustain[safe_layer] = static_cast<uint8_t>(value);
            return;
        default:
            app.perform_adsr_loop_release[safe_layer] = value;
            return;
    }
}

static int PerformAdsrStageMin(uint8_t stage)
{
    return (stage % static_cast<uint8_t>(kAdsrStageCount)) == 2u
               ? 0
               : static_cast<int>(kPerformAdsrAttackReleaseMinMs);
}

static int PerformAdsrStageMax(uint8_t stage)
{
    switch(stage % static_cast<uint8_t>(kAdsrStageCount))
    {
        case 0:
        case 3: return static_cast<int>(kPerformAdsrAttackReleaseMaxMs);
        case 1: return static_cast<int>(kPerformAdsrDecayMaxMs);
        case 2:
        default: return static_cast<int>(kPerformAdsrSustainMax);
    }
}

static uint8_t& PerformAdsrRow(AppState& app, uint8_t layer)
{
    return app.perform_adsr_row[layer & 1u];
}

static uint8_t& PerformAdsrEnvX(AppState& app, uint8_t layer, uint8_t stage)
{
    const uint8_t safe_layer = layer & 1u;
    switch(stage % static_cast<uint8_t>(kAdsrStageCount))
    {
        case 0: return app.perform_adsr_env_a_x[safe_layer];
        case 1: return app.perform_adsr_env_d_x[safe_layer];
        default: return app.perform_adsr_env_r_x[safe_layer];
    }
}

static uint8_t& PerformAdsrEnvSLevel(AppState& app, uint8_t layer)
{
    return app.perform_adsr_env_s_level[layer & 1u];
}

static bool PerformAdsrStageEnabled(uint8_t adsr_row, uint8_t stage)
{
    const uint8_t safe_row = adsr_row % static_cast<uint8_t>(kAdsrRowCount);
    const uint8_t safe_stage = stage % static_cast<uint8_t>(kAdsrStageCount);
    if(safe_row == static_cast<uint8_t>(kAdsrRowOneShot)
       && (safe_stage == 1u || safe_stage == 2u))
        return false;
    return true;
}

static bool PerformAdsrWaveFocusable(uint8_t adsr_row)
{
    return (adsr_row % static_cast<uint8_t>(kAdsrRowCount)) == static_cast<uint8_t>(kAdsrRowLoop);
}

static int PerformAdsrFocusIndex(const AppState& app, uint8_t layer)
{
    if(app.perform_adsr_type_focus)
        return 0;

    const uint8_t adsr_row = app.perform_adsr_row[layer & 1u];
    if(PerformAdsrWaveFocusable(adsr_row) && app.perform_adsr_wave_focus)
        return 1;

    return static_cast<int>(app.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount))
           + (PerformAdsrWaveFocusable(adsr_row) ? 2 : 1);
}

static void PerformAdsrSetFocusIndex(AppState& app, uint8_t layer, int idx)
{
    const uint8_t adsr_row = app.perform_adsr_row[layer & 1u];
    if(idx <= 0)
    {
        app.perform_adsr_type_focus = true;
        app.perform_adsr_wave_focus = false;
        return;
    }

    if(PerformAdsrWaveFocusable(adsr_row) && idx == 1)
    {
        app.perform_adsr_type_focus = false;
        app.perform_adsr_wave_focus = true;
        return;
    }

    app.perform_adsr_type_focus = false;
    app.perform_adsr_wave_focus = false;
    const int stage_base = PerformAdsrWaveFocusable(adsr_row) ? 2 : 1;
    app.perform_adsr_stage_focus
        = static_cast<uint8_t>(ClampInt(idx - stage_base, 0, kAdsrStageCount - 1));
}

static void PerformAdsrEnsureValidFocus(AppState& app, uint8_t layer)
{
    const uint8_t adsr_row = PerformAdsrRow(app, layer);
    if(app.perform_adsr_type_focus)
    {
        app.perform_adsr_wave_focus = false;
        return;
    }

    if(!PerformAdsrWaveFocusable(adsr_row))
        app.perform_adsr_wave_focus = false;
    if(app.perform_adsr_wave_focus)
        return;

    const uint8_t stage = app.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
    if(PerformAdsrStageEnabled(adsr_row, stage))
        return;

    app.perform_adsr_stage_focus = (stage <= 1u) ? 0u : 3u;
}

void ExtractBaseName(const char* path, char* out, size_t out_n)
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

static void FormatMidiNoteName(uint8_t note, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;

    static const char* kNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
    };
    const int midi_note = static_cast<int>(note);
    const int pitch = ((midi_note % 12) + 12) % 12;
    const int octave = (midi_note / 12) - 1;
    std::snprintf(out, out_n, "%s%d", kNames[pitch], octave);
}

static void FormatDbTenths(int v_tenths, char* out, size_t out_n)
{
    const int sign = (v_tenths < 0) ? -1 : 1;
    const int abs_tenths = (v_tenths < 0) ? -v_tenths : v_tenths;
    const int whole = abs_tenths / 10;
    const int frac = abs_tenths % 10;
    if(sign < 0)
        std::snprintf(out, out_n, "-%d.%ddB", whole, frac);
    else
        std::snprintf(out, out_n, "%d.%ddB", whole, frac);
}

static uint8_t ClampDriveMode(int value)
{
    if(value <= 0)
        return 0u;
    return 1u;
}

static const char* DriveModeLabel(uint8_t mode)
{
    return (ClampDriveMode(static_cast<int>(mode)) == 0u) ? "odd" : "even";
}

static constexpr float kProcessLayerLevelUiMax = 2.0f; // Match existing Daisy PROCESS edit clamp.

static void FormatProcessLevelDb(float level, char* out, size_t out_n)
{
    if(out == nullptr || out_n == 0)
        return;

    if(level <= 0.00001f)
    {
        std::snprintf(out, out_n, "-infdb");
        return;
    }

    const float db = 20.0f * std::log10(level);
    if(db > 0.049f)
        std::snprintf(out, out_n, "+%.1fdb", static_cast<double>(db));
    else if(db < -0.049f)
        std::snprintf(out, out_n, "%.1fdb", static_cast<double>(db));
    else
        std::snprintf(out, out_n, "0.0db");
}

static float ProcessLevelToKnobNorm(float level)
{
    if(level <= 0.00001f)
        return 0.0f;
    if(level <= 1.0f)
    {
        float db = 20.0f * std::log10(level);
        if(db < -60.0f)
            db = -60.0f;
        return ((db + 60.0f) / 60.0f) * 0.5f;
    }

    float db = 20.0f * std::log10(level);
    const float max_db = 20.0f * std::log10(kProcessLayerLevelUiMax);
    if(db > max_db)
        db = max_db;
    if(max_db <= 0.0f)
        return 0.5f;
    return 0.5f + (db / max_db) * 0.5f;
}

static void PublishEngineLayerParams(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.params)
        return;

    AppState& app = *ctx.app;
    const uint8_t layer = app.perform_layer & 1u;
    PerformParamsTargets& t = ctx.params->EditTargets();
    t.engine_tune_semitones[layer] = static_cast<float>(app.engine_tune_semitones[layer]);
    t.engine_gain_db[layer] = static_cast<float>(app.engine_gain_db[layer]);
    t.engine_loop_mode[layer] = (app.engine_play_mode[layer] != 0);
    for(uint8_t i = 0; i < kPerformLayerCount; ++i)
    {
        const uint16_t clamped_attack = static_cast<uint16_t>(
            ClampInt(static_cast<int>(app.perform_adsr_loop_attack[i]),
                     static_cast<int>(kPerformAdsrAttackReleaseMinMs),
                     static_cast<int>(kPerformAdsrAttackReleaseMaxMs)));
        const uint16_t clamped_release = static_cast<uint16_t>(
            ClampInt(static_cast<int>(app.perform_adsr_loop_release[i]),
                     static_cast<int>(kPerformAdsrAttackReleaseMinMs),
                     static_cast<int>(kPerformAdsrAttackReleaseMaxMs)));
        const uint8_t clamped_decay = static_cast<uint8_t>(
            ClampInt(static_cast<int>(app.perform_adsr_loop_decay[i]), 1, static_cast<int>(kPerformAdsrDecayMaxMs)));
        const uint8_t clamped_sustain = static_cast<uint8_t>(
            ClampInt(static_cast<int>(app.perform_adsr_loop_sustain[i]), 0, static_cast<int>(kPerformAdsrSustainMax)));
        app.perform_adsr_loop_attack[i] = clamped_attack;
        app.perform_adsr_loop_decay[i] = clamped_decay;
        app.perform_adsr_loop_sustain[i] = clamped_sustain;
        app.perform_adsr_loop_release[i] = clamped_release;
        t.engine_drive_mode[i] = ClampDriveMode(static_cast<int>(app.engine_drive_mode[i]));
        t.perform_keyzone_lo_note[i] = app.perform_keyzone_lo_note[i];
        t.perform_keyzone_hi_note[i] = app.perform_keyzone_hi_note[i];
        t.engine_loop_attack_ms[i] = static_cast<float>(clamped_attack);
        t.engine_loop_decay_ms[i] = static_cast<float>(clamped_decay);
        t.engine_loop_sustain_level[i] = static_cast<float>(clamped_sustain) * 0.01f;
        t.engine_loop_release_ms[i] = static_cast<float>(clamped_release);
        t.engine_loop_crossfade_amount[i] = app.perform_adsr_loop_crossfade[i];
        t.engine_loop_crossfade_shape[i] = app.perform_adsr_loop_crossfade_shape[i];
    }
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
                                int h,
                                bool on,
                                bool outline_only,
                                bool dotted_border)
{
    if(w < 3 || h < 3)
        return;

    const int x0 = x;
    const int y0 = y;
    const int x1 = x + w - 1;
    const int y1 = y + h - 1;
    if(dotted_border)
        DrawDottedRect(d, x0, y0, x1, y1, on);
    else
        d.DrawRect(x0, y0, x1, y1, on, false);

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
    bool have_prev = false;
    int prev_x = 0;
    int prev_top = 0;
    int prev_bot = 0;
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

        if(outline_only)
        {
            if(have_prev)
            {
                d.DrawLine(prev_x, prev_top, xx, top, on);
                d.DrawLine(prev_x, prev_bot, xx, bot, on);
            }
            else
            {
                d.DrawPixel(xx, top, on);
                d.DrawPixel(xx, bot, on);
            }
            have_prev = true;
            prev_x = xx;
            prev_top = top;
            prev_bot = bot;
            continue;
        }

        for(int yy = top; yy <= bot; ++yy)
            d.DrawPixel(xx, yy, on);
    }
}

static void DrawPerformLoopCrossfadeCurve(OledPager& d,
                                          int x0,
                                          int y0,
                                          int x1,
                                          int y1,
                                          float shape,
                                          bool fade_in)
{
    if(x1 < x0 || y1 < y0)
        return;

    const int w = x1 - x0 + 1;
    const int h = y1 - y0 + 1;
    if(w <= 0 || h <= 0 || w > 128)
        return;

    int y_top[128] = {};
    int y_bottom[128] = {};
    for(int i = 0; i < w; ++i)
    {
        const float mix = (w <= 1) ? 0.0f : (static_cast<float>(i) / static_cast<float>(w - 1));
        const float weight = ComputePerformLoopCrossfadeWeight(mix, shape, fade_in);
        int center_y = y1 - static_cast<int>(weight * static_cast<float>(h - 1) + 0.5f);
        if(center_y < y0)
            center_y = y0;
        if(center_y > y1)
            center_y = y1;

        int top = center_y;
        int bottom = center_y + 1;
        if(bottom > y1)
        {
            bottom = y1;
            top = (y1 > y0) ? (y1 - 1) : y0;
        }

        y_top[i] = top;
        y_bottom[i] = bottom;
    }

    for(int i = 0; i < w; ++i)
    {
        const int xx = x0 + i;
        for(int dx = -2; dx <= 2; ++dx)
        {
            const int px = xx + dx;
            if(px < x0 || px > x1)
                continue;
            for(int py = y_top[i] - 2; py <= y_bottom[i] + 2; ++py)
            {
                if(py < y0 || py > y1)
                    continue;
                d.DrawPixel(px, py, false);
            }
        }
    }

    int prev_x = x0;
    int prev_top = y_top[0];
    int prev_bottom = y_bottom[0];
    d.DrawPixel(prev_x, prev_top, true);
    d.DrawPixel(prev_x, prev_bottom, true);
    for(int i = 1; i < w; ++i)
    {
        const int xx = x0 + i;
        d.DrawLine(prev_x, prev_top, xx, y_top[i], true);
        d.DrawLine(prev_x, prev_bottom, xx, y_bottom[i], true);
        prev_x = xx;
        prev_top = y_top[i];
        prev_bottom = y_bottom[i];
    }
}

void PerformEngine_OnScreenEnter(UiScreenCtx& ctx)
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
    ctx.app->perform_engine_row = static_cast<uint8_t>(kEngineRowLoad);
    PublishEngineLayerParams(ctx);
    ctx.app->ui_dirty = true;
}

bool PerformEngine_OnEnter(UiScreenCtx& ctx)
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

bool PerformEngine_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
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
        app.engine_header_invert_until_ms = e.t_ms + 250u;
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

void PerformEngine_Render(UiScreenCtx& ctx)
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
    char header_label[16] = {};
    std::snprintf(header_label, sizeof(header_label), "engine %c", layer == 0 ? 'a' : 'b');
    const int header_w = MicroStringWidth(header_label);
    const int box_w = header_w + 4;
    const int box_h = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash = static_cast<int32_t>(app.engine_header_invert_until_ms - ctx.now_ms) > 0;
    if(header_invert_flash)
    {
        // Inverted phase: dark fill with white text/border.
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, false, true);
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, false);
        DrawMicroString(d, header_label, box_x + 2, 2, true);
    }
    else
    {
        // Default phase: filled white box with dark text.
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, true);
        DrawMicroString(d, header_label, box_x + 2, 2, false);
    }

    constexpr int kTopTextX = 2;
    constexpr int kTopTextY = 2;
    const int top_text_bottom_y = kTopTextY + Font5x7::H - 1;
    if(!sample_loaded)
    {
        DrawTinyString(d, "no sample", kTopTextX, kTopTextY, true);
    }
    else
    {
        const char* name = app.engine_sample_name[layer];
        if(name == nullptr)
            name = "";
        char name_buf[40];
        ToLowerCase(name, name_buf, sizeof(name_buf));
        char clipped[40];
        clipped[0] = '\0';
        const int max_name_w = box_x - kTopTextX - 1; // keep clear of the header box.
        if(max_name_w > 0)
        {
            const int char_advance = Font5x7::W + 1;
            const int max_chars = (max_name_w + 1) / char_advance;
            int i = 0;
            for(; name_buf[i] != '\0' && i < max_chars && i + 1 < static_cast<int>(sizeof(clipped)); ++i)
                clipped[i] = name_buf[i];
            clipped[i] = '\0';
        }
        DrawTinyString(d, clipped, kTopTextX, kTopTextY, true);
    }

    constexpr int kWaveX = 0;
    const int kWaveY = top_text_bottom_y + 2;
    constexpr int kWaveW = 128;
    constexpr int kScreenH = 64;
    int kWaveBottomY = layout.y_footer - 9;
    if(kWaveBottomY < kWaveY)
        kWaveBottomY = kWaveY;
    const int kWaveH = kWaveBottomY - kWaveY + 1;

    const int footer_region_top = kWaveBottomY + 1;
    const int footer_region_bottom = kScreenH - 1;
    const int footer_region_h = footer_region_bottom - footer_region_top + 1;
    int kFooterY = footer_region_top;
    if(footer_region_h > Font5x7::H)
        kFooterY += (footer_region_h - Font5x7::H) / 2;

    DrawWaveformPreview(d, sample, edit, kWaveX, kWaveY, kWaveW, kWaveH, true);
    const uint8_t row = app.perform_engine_row % static_cast<uint8_t>(kEngineRowCount);
    if(row == kEngineRowWave)
    {
        // Invert full waveform preview region to signal enterable deep menu.
        d.DrawRect(kWaveX, kWaveY, kWaveX + kWaveW - 1, kWaveY + kWaveH - 1, true, true);
        DrawWaveformPreview(d, sample, edit, kWaveX, kWaveY, kWaveW, kWaveH, false);
    }

    const int load_w = LoadWordmarkWidth();
    const int tune_w = TuneWordmarkWidth();
    constexpr int kScreenW = 128;
    constexpr int kHalfW = kScreenW / 2;
    const int load_anchor_x = kHalfW / 2;
    const int tune_anchor_x = kHalfW + (kHalfW / 2);
    int load_x = load_anchor_x - (load_w / 2);
    int tune_x = tune_anchor_x - (tune_w / 2);
    const int load_min_x = 1;
    const int load_max_x = (kHalfW - 1) - load_w;
    const int tune_min_x = kHalfW + 1;
    const int tune_max_x = (kScreenW - 1) - tune_w;
    if(load_x < load_min_x)
        load_x = load_min_x;
    if(load_x > load_max_x)
        load_x = load_max_x;
    if(tune_x < tune_min_x)
        tune_x = tune_min_x;
    if(tune_x > tune_max_x)
        tune_x = tune_max_x;

    if(row == kEngineRowLoad)
    {
        d.DrawRect(load_x - 2, kFooterY - 2, load_x + load_w + 1, kFooterY + Font5x7::H + 1, true, true);
        DrawLoadWordmark(d, load_x, kFooterY, false);
    }
    else
    {
        DrawLoadWordmark(d, load_x, kFooterY, true);
    }

    if(row == kEngineRowTune)
    {
        DrawDottedRect(d, tune_x - 2, kFooterY - 2, tune_x + tune_w + 1, kFooterY + Font5x7::H + 1, true);
        const int semitones = static_cast<int>(app.engine_tune_semitones[layer]);
        const int val_w = SignedSemitoneTextWidth(semitones);
        int val_x = tune_x + (tune_w - val_w) / 2;
        if(val_x < tune_x)
            val_x = tune_x;
        DrawSignedSemitoneText(d, semitones, val_x, kFooterY, true);
    }
    else
    {
        DrawTuneWordmark(d, tune_x, kFooterY, true);
    }
}

void PerformWaveEdit_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

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

    const int wave_x = 0;
    const int wave_y = 0;
    const int wave_w = 128;
    const int wave_h = 64;
    const int x0 = wave_x;
    const int y0 = wave_y;
    const int x1 = wave_x + wave_w - 1;
    const int y1 = wave_y + wave_h - 1;
    d.DrawRect(x0, y0, x1, y1, true, false);

    if(!sample_loaded)
    {
        DrawTinyString(d, "no sample", 2, 2, true);
        return;
    }

    if(wave_w >= 3 && wave_h >= 3)
    {
        const int preview_bottom_y = y1 - (kMini3x5H + 3);
        const int waveform_y0 = y0 + 1;
        const int waveform_y1 = preview_bottom_y - 1;
        if(waveform_y1 <= waveform_y0)
            return;

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

        const int waveform_h = waveform_y1 - waveform_y0 + 1;
        const int mid = waveform_y0 + waveform_h / 2;
        const int amp_h = (waveform_h - 1) / 2;

        // Invert selected trim window.
        d.DrawRect(start_x, waveform_y0, end_x, waveform_y1, true, true);

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
            if(top < waveform_y0) top = waveform_y0;
            if(bot > waveform_y1) bot = waveform_y1;
            if(bot < top) bot = top;

            const int xx = x0 + 1 + px;
            const bool inside = (xx >= start_x && xx <= end_x);
            if(inside)
            {
                d.DrawLine(xx, top, xx, bot, false);
            }
            else
            {
                for(int yy = top; yy <= bot; ++yy)
                {
                    if((yy & 1) == 0)
                        d.DrawPixel(xx, yy, true);
                }
            }
        }

        d.DrawLine(start_x, waveform_y0, start_x, waveform_y1, true);
        d.DrawLine(end_x, waveform_y0, end_x, waveform_y1, true);

        // Solid divider at bottom of waveform preview area.
        d.DrawLine(x0 + 1, preview_bottom_y, x1 - 1, preview_bottom_y, true);

        const int start_w = MiniString3x5Width("start");
        const int end_w = MiniString3x5Width("end");
        const int label_y = preview_bottom_y + 2;
        const int min_gap = 1;
        const int start_min_x = x0 + 1;
        const int end_min_x = x0 + 1;
        const int start_max_x = x1 - start_w;
        const int end_max_x = x1 - end_w;

        int start_label_x = start_x - (start_w / 2);
        int end_label_x = end_x - (end_w / 2);

        if(start_label_x < start_min_x) start_label_x = start_min_x;
        if(start_label_x > start_max_x) start_label_x = start_max_x;
        if(end_label_x < end_min_x) end_label_x = end_min_x;
        if(end_label_x > end_max_x) end_label_x = end_max_x;

        // Keep labels disjoint even when trim lines get very close.
        if(start_label_x + start_w + min_gap > end_label_x)
        {
            const int overlap = (start_label_x + start_w + min_gap) - end_label_x;
            start_label_x -= (overlap + 1) / 2;
            end_label_x += overlap / 2;

            if(start_label_x < start_min_x) start_label_x = start_min_x;
            if(end_label_x > end_max_x) end_label_x = end_max_x;

            if(start_label_x + start_w + min_gap > end_label_x)
            {
                end_label_x = start_label_x + start_w + min_gap;
                if(end_label_x > end_max_x)
                {
                    end_label_x = end_max_x;
                    start_label_x = end_label_x - start_w - min_gap;
                    if(start_label_x < start_min_x)
                        start_label_x = start_min_x;
                }
            }
        }

        DrawMiniString3x5(d, "start", start_label_x, label_y, true);
        DrawMiniString3x5(d, "end", end_label_x, label_y, true);

        const uint32_t ph_active = app.playhead_active[layer].load(std::memory_order_relaxed);
        if(ph_active != 0u)
        {
            const uint32_t ph_frame = app.playhead_frame[layer].load(std::memory_order_relaxed);
            const uint32_t ph = (ph_frame >= frames) ? (frames - 1) : ph_frame;
            const int play_x = x0 + static_cast<int>((static_cast<uint64_t>(ph) * (wave_w - 1)) / denom);
            d.DrawLine(play_x, waveform_y0, play_x, waveform_y1, true);
        }
    }

}

void PerformWaveEdit_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;

    AppState& app = *ctx.app;
    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
        app.perform_wave_edit_entry[slot] = app.sd_edit_slots[slot];
    app.perform_wave_edit_has_entry = true;
    app.ui_dirty = true;
}

bool PerformWaveEdit_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return false;

    AppState& app = *ctx.app;
    const uint8_t layer = app.perform_layer & 1u;
    SampleEdit edit = app.sd_edit_slots[layer];
    const Sample& sample = app.sd_slots[layer];
    SampleEdit_Clamp(edit, sample.length);
    app.sd_edit_slots[layer] = edit;
    app.sd_edit_pending = edit;
    app.sd_edit_slot.store(layer, std::memory_order_release);
    app.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
    app.sd_edit_ready.store(1, std::memory_order_release);
    app.perform_wave_edit_has_entry = false;
    UiNav_Pop(app.ui_nav);
    app.ui_dirty = true;
    return true;
}

bool PerformWaveEdit_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
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

    // Cancel trim edit session: restore entry snapshot and return.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        if(app.perform_wave_edit_has_entry)
        {
            for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
                app.sd_edit_slots[slot] = app.perform_wave_edit_entry[slot];
            app.perform_wave_edit_has_entry = false;
        }
        UiNav_Pop(app.ui_nav);
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
        app.ui_dirty = true;
        return true;
    }

    return false;
}

bool PerformKeyzone_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    if(ctx.lshift)
        return false;

    AppState& app = *ctx.app;

    // POD2 toggles layer and keeps focus on the BACK (start) marker.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        app.perform_layer ^= 1u;
        const uint8_t layer = app.perform_layer & 1u;
        app.perform_keyzone_marker_focus = static_cast<uint8_t>(layer * 2u);
        app.sd_current_slot.store(layer, std::memory_order_release);
        app.engine_header_invert_until_ms = e.t_ms + 250u;
        PublishEngineLayerParams(ctx);
        app.ui_dirty = true;
        return true;
    }

    if(ctx.rshift && e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        const bool full_a = app.perform_keyzone_lo_note[0] == kPerformKeyzoneMinNote
                            && app.perform_keyzone_hi_note[0] == kPerformKeyzoneMaxNote;
        const bool full_b = app.perform_keyzone_lo_note[1] == kPerformKeyzoneMinNote
                            && app.perform_keyzone_hi_note[1] == kPerformKeyzoneMaxNote;
        if(full_a && full_b)
        {
            app.perform_keyzone_lo_note[0] = kPerformKeyzoneMinNote;
            app.perform_keyzone_hi_note[0] = kPerformKeyzoneDefaultSplitAHi;
            app.perform_keyzone_lo_note[1] = kPerformKeyzoneDefaultSplitBLo;
            app.perform_keyzone_hi_note[1] = kPerformKeyzoneMaxNote;
        }
        else
        {
            for(uint8_t i = 0; i < kPerformLayerCount; ++i)
            {
                app.perform_keyzone_lo_note[i] = kPerformKeyzoneMinNote;
                app.perform_keyzone_hi_note[i] = kPerformKeyzoneMaxNote;
            }
        }
        PublishEngineLayerParams(ctx);
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.value != 0)
    {
        const uint8_t layer = app.perform_layer & 1u;
        if(ctx.rshift)
        {
            if(e.id != kUiEncPod && e.id != kUiEncExt)
                return false;
            const int dir = (e.value < 0) ? -1 : 1;
            const int lo = static_cast<int>(app.perform_keyzone_lo_note[layer]);
            const int hi = static_cast<int>(app.perform_keyzone_hi_note[layer]);
            const int delta_min = static_cast<int>(kPerformKeyzoneMinNote) - lo;
            const int delta_max = static_cast<int>(kPerformKeyzoneMaxNote) - hi;
            const int applied = ClampInt(dir, delta_min, delta_max);
            if(applied == 0)
                return false;
            app.perform_keyzone_lo_note[layer] = static_cast<uint8_t>(lo + applied);
            app.perform_keyzone_hi_note[layer] = static_cast<uint8_t>(hi + applied);
            PublishEngineLayerParams(ctx);
            app.ui_dirty = true;
            return true;
        }

        if(e.id == kUiEncPod)
        {
            const int next_lo = ClampInt(static_cast<int>(app.perform_keyzone_lo_note[layer]) + e.value,
                                         static_cast<int>(kPerformKeyzoneMinNote),
                                         static_cast<int>(app.perform_keyzone_hi_note[layer]));
            if(next_lo == static_cast<int>(app.perform_keyzone_lo_note[layer]))
                return false;
            app.perform_keyzone_lo_note[layer] = static_cast<uint8_t>(next_lo);
            app.perform_keyzone_marker_focus = static_cast<uint8_t>(layer * 2u); // back marker
            app.ui_dirty = true;
            PublishEngineLayerParams(ctx);
            return true;
        }

        if(e.id == kUiEncExt)
        {
            const int next_hi = ClampInt(static_cast<int>(app.perform_keyzone_hi_note[layer]) + e.value,
                                         static_cast<int>(app.perform_keyzone_lo_note[layer]),
                                         static_cast<int>(kPerformKeyzoneMaxNote));
            if(next_hi == static_cast<int>(app.perform_keyzone_hi_note[layer]))
                return false;
            app.perform_keyzone_hi_note[layer] = static_cast<uint8_t>(next_hi);
            app.perform_keyzone_marker_focus = static_cast<uint8_t>((layer * 2u) + 1u); // forward marker
            PublishEngineLayerParams(ctx);
            app.ui_dirty = true;
            return true;
        }
    }
    return false;
}

bool PerformAdsr_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    if(ctx.shift)
        return false;

    AppState& app = *ctx.app;

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        app.perform_layer ^= 1u;
        const uint8_t layer = app.perform_layer & 1u;
        app.sd_current_slot.store(layer, std::memory_order_release);
        app.engine_header_invert_until_ms = e.t_ms + 250u;
        PerformAdsrEnsureValidFocus(app, layer);
        PublishEngineLayerParams(ctx);
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        const uint8_t layer = app.perform_layer & 1u;
        const uint8_t adsr_row = PerformAdsrRow(app, layer);
        int idx = PerformAdsrFocusIndex(app, layer);
        const bool wave_focusable = PerformAdsrWaveFocusable(adsr_row);
        const int focus_count = kAdsrStageCount + (wave_focusable ? 2 : 1);
        const int stage_base = wave_focusable ? 2 : 1;
        int steps = (e.value < 0) ? -e.value : e.value;
        const int dir = (e.value < 0) ? -1 : 1;
        while(steps-- > 0)
        {
            do
            {
                idx += dir;
                while(idx < 0)
                    idx += focus_count;
                while(idx >= focus_count)
                    idx -= focus_count;
            } while(idx >= stage_base
                    && !PerformAdsrStageEnabled(adsr_row,
                                                static_cast<uint8_t>(idx - stage_base)));
        }

        PerformAdsrSetFocusIndex(app, layer, idx);
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const uint8_t layer = app.perform_layer & 1u;
        uint8_t& adsr_row = PerformAdsrRow(app, layer);
        if(app.perform_adsr_type_focus)
        {
            int row = static_cast<int>(adsr_row % static_cast<uint8_t>(kAdsrRowCount));
            row += e.value;
            while(row < 0)
                row += kAdsrRowCount;
            while(row >= kAdsrRowCount)
                row -= kAdsrRowCount;
            adsr_row = static_cast<uint8_t>(row);

            uint8_t next_mode = app.engine_play_mode[layer] & 1u;
            bool changed = false;
            if(row == static_cast<int>(kAdsrRowOneShot))
            {
                if(next_mode != 0u)
                {
                    next_mode = 0u;
                    changed = true;
                }
            }
            else if(row == static_cast<int>(kAdsrRowLoop))
            {
                if(next_mode != 1u)
                {
                    next_mode = 1u;
                    changed = true;
                }
            }

            if(changed)
            {
                app.engine_play_mode[layer] = next_mode;
                PublishEngineLayerParams(ctx);
            }
            PerformAdsrEnsureValidFocus(app, layer);
            app.ui_dirty = true;
            return true;
        }

        if(app.perform_adsr_wave_focus
           && (adsr_row % static_cast<uint8_t>(kAdsrRowCount)) == static_cast<uint8_t>(kAdsrRowLoop))
        {
            float& target = ctx.rshift ? app.perform_adsr_loop_crossfade_shape[layer]
                                       : app.perform_adsr_loop_crossfade[layer];
            const float step = ctx.rshift ? kPerformLoopCrossfadeShapeStep
                                          : kPerformLoopCrossfadeStep;
            const float min_value = ctx.rshift ? kPerformLoopCrossfadeShapeMin
                                               : kPerformLoopCrossfadeMin;
            const float max_value = ctx.rshift ? kPerformLoopCrossfadeShapeMax
                                               : kPerformLoopCrossfadeMax;
            float next = target + (static_cast<float>(e.value) * step);
            if(next < min_value)
                next = min_value;
            if(next > max_value)
                next = max_value;
            if(next == target)
                return false;
            target = next;
            PublishEngineLayerParams(ctx);
            app.ui_dirty = true;
            return true;
        }

        if((adsr_row % static_cast<uint8_t>(kAdsrRowCount)) != static_cast<uint8_t>(kAdsrRowLoop))
        {
            if((adsr_row % static_cast<uint8_t>(kAdsrRowCount)) != static_cast<uint8_t>(kAdsrRowAdsr))
                return false;

            const uint8_t stage = app.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
            if(stage == 2u)
            {
                uint8_t& level = PerformAdsrEnvSLevel(app, layer);
                const int next_level = ClampInt(static_cast<int>(level) + e.value, 0, 100);
                if(next_level == static_cast<int>(level))
                    return false;
                level = static_cast<uint8_t>(next_level);
                app.ui_dirty = true;
                return true;
            }

            static constexpr int kAdsrEnvMinGap = 6;
            uint8_t& value = PerformAdsrEnvX(app, layer, stage);
            const int a_x = static_cast<int>(app.perform_adsr_env_a_x[layer]);
            const int d_x = static_cast<int>(app.perform_adsr_env_d_x[layer]);
            const int r_x = static_cast<int>(app.perform_adsr_env_r_x[layer]);
            int min_value = 0;
            int max_value = 100;
            if(stage == 0u)
            {
                max_value = d_x - kAdsrEnvMinGap;
            }
            else if(stage == 1u)
            {
                min_value = a_x + kAdsrEnvMinGap;
                max_value = r_x - kAdsrEnvMinGap;
            }
            else
            {
                min_value = d_x + kAdsrEnvMinGap;
            }

            const int next_value = ClampInt(static_cast<int>(value) + e.value, min_value, max_value);
            if(next_value == static_cast<int>(value))
                return false;
            value = static_cast<uint8_t>(next_value);
            app.ui_dirty = true;
            return true;
        }

        const uint8_t stage = app.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
        const uint16_t value = PerformAdsrStageValue(app, layer, stage);
        const int min_value = PerformAdsrStageMin(stage);
        const int max_value = PerformAdsrStageMax(stage);
        const int next_value = ClampInt(static_cast<int>(value) + e.value, min_value, max_value);
        if(next_value == static_cast<int>(value))
            return false;

        SetPerformAdsrStageValue(app, layer, stage, static_cast<uint16_t>(next_value));
        PublishEngineLayerParams(ctx);
        app.ui_dirty = true;
        return true;
    }

    return false;
}

void PerformAdsr_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    AppState& app = *ctx.app;
    app.perform_adsr_stage_focus = 0;
    app.perform_adsr_type_focus = false;
    app.perform_adsr_wave_focus = false;
    const uint8_t layer = app.perform_layer & 1u;
    uint8_t& adsr_row = PerformAdsrRow(app, layer);
    if(adsr_row >= static_cast<uint8_t>(kAdsrRowCount))
        adsr_row = (app.engine_play_mode[layer] & 1u) ? kAdsrRowLoop : kAdsrRowOneShot;
    PerformAdsrEnsureValidFocus(app, layer);
    app.ui_dirty = true;
}

bool PerformEmphasis_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app || !ctx.params)
        return false;
    if(ctx.shift)
        return false;

    AppState& app = *ctx.app;
    const uint8_t layer = app.perform_layer & 1u;

    static uint32_t s_last_ext_t_ms = 0u;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        int row = static_cast<int>(app.perform_emphasis_row) + e.value;
        while(row < 0) row += 3;
        while(row >= 3) row -= 3;
        app.perform_emphasis_row = static_cast<uint8_t>(row);
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const float delta_norm = UiDeltaNormAccelerated(e.value, e.t_ms, s_last_ext_t_ms, 0.02f);
        if(app.perform_emphasis_row == 0)
        {
            if(ctx.rshift)
            {
                int mode = static_cast<int>(app.engine_drive_mode[layer]) + e.value;
                while(mode < 0)
                    mode += 2;
                while(mode >= 2)
                    mode -= 2;
                const uint8_t next_mode = ClampDriveMode(mode);
                if(next_mode != app.engine_drive_mode[layer])
                {
                    app.engine_drive_mode[layer] = next_mode;
                    PublishEngineLayerParams(ctx);
                    app.ui_dirty = true;
                }
                return true;
            }

            // DRIVE row: 0.0 dB at 7 o'clock through +6.0 dB at 5 o'clock in 0.1 dB steps.
            int v = static_cast<int>(app.engine_gain_db[layer]) + e.value;
            v = ClampInt(v, 0, 60);
            const int16_t vv = static_cast<int16_t>(v);
            if(vv != app.engine_gain_db[layer])
            {
                app.engine_gain_db[layer] = vv;
                PublishEngineLayerParams(ctx);
                app.ui_dirty = true;
            }
            return true;
        }

        PerformParamsTargets& t = ctx.params->EditTargets();
        if(app.perform_emphasis_row == 1)
        {
            // Keep fast fader motion; map fader space to cutoff using ADSR-style curve.
            float fader = AdsrFltFaderFromCutoffHz(t.engine_filter_cutoff_hz[layer]);
            fader = Clamp01(fader + delta_norm);
            t.engine_filter_cutoff_hz[layer] = AdsrFltCutoffHzFromFader(fader);
            ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }

        // RESONANCE
        t.engine_filter_resonance[layer] = Clamp01(t.engine_filter_resonance[layer] + delta_norm);
        ctx.params->PublishTargets();
        app.ui_dirty = true;
        return true;
    }

    // POD2 toggles layer (same behavior as ENGINE).
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        app.perform_layer ^= 1u;
        const uint8_t layer = app.perform_layer & 1u;
        app.sd_current_slot.store(layer, std::memory_order_release);
        app.engine_header_invert_until_ms = e.t_ms + 250u;
        PublishEngineLayerParams(ctx);
        app.ui_dirty = true;
        return true;
    }

    return false;
}

void PerformEmphasis_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    ctx.app->ui_dirty = true;
}

static int KeyzonePageFromNote(uint8_t note)
{
    // Pages are C0-B0 .. C8-B8, clamped to the supported note range.
    const int octave = (static_cast<int>(note) / 12) - 1;
    return ClampInt(octave, 0, 8);
}

static void DrawKeyzoneUiRangeTemplate(
    OledPager& d, AppState& app, uint8_t layer, bool highlight_both, int x0, int y0)
{
    constexpr int kAreaW = 128;
    constexpr int kVisibleOctaves = 3;
    constexpr int kPageCount = 9; // C0-B0 .. C8-B8
    constexpr int kWhiteKeyCount = 7 * kVisibleOctaves;
    // Geometry matched to the provided reference image: wide 3-octave span,
    // shallow white keys, and ~60% black-key height.
    constexpr int kWhiteW = 6;
    constexpr int kWhiteH = 28;
    constexpr int kBlackW = 3;
    constexpr int kBlackH = 17;

    const int total_white_w = kWhiteKeyCount * kWhiteW;
    const int kb_x0 = x0 + ((kAreaW - total_white_w) / 2);
    // Use upper available space and preserve blank space below for future UI.
    const int kb_y0 = y0 + 2;
    const int kb_x1 = kb_x0 + total_white_w - 1;
    const int kb_y1 = kb_y0 + kWhiteH - 1;

    // Real-keyboard contrast on monochrome OLED: white bed + black key cutouts.
    d.DrawRect(kb_x0, kb_y0, kb_x1, kb_y1, true, true);
    for(int i = 1; i < kWhiteKeyCount; ++i)
    {
        const int x = kb_x0 + (i * kWhiteW);
        d.DrawLine(x, kb_y0 + 1, x, kb_y1 - 1, false);
    }

    // Black keys above C,D,F,G,A for each visible octave.
    const int black_after_white_idx[5] = {0, 1, 3, 4, 5};
    for(int oct = 0; oct < kVisibleOctaves; ++oct)
    {
        for(int i = 0; i < 5; ++i)
        {
            const int after_white = (oct * 7) + black_after_white_idx[i];
            const int center_x = kb_x0 + ((after_white + 1) * kWhiteW);
            const int bx0 = center_x - (kBlackW / 2);
            const int bx1 = bx0 + kBlackW - 1;
            const int by0 = kb_y0;
            const int by1 = kb_y0 + kBlackH - 1;
            d.DrawRect(bx0, by0, bx1, by1, false, true);
        }
    }

    const uint8_t lo_note = app.perform_keyzone_lo_note[layer & 1u];
    const uint8_t hi_note = app.perform_keyzone_hi_note[layer & 1u];
    const uint8_t focused_marker = app.perform_keyzone_marker_focus & 1u; // 0=LO, 1=HI
    const uint8_t focus_note = (focused_marker == 0u) ? lo_note : hi_note;
    int window_octave = static_cast<int>(app.perform_keyzone_window_octave[layer & 1u]);
    window_octave = ClampInt(window_octave, 0, kPageCount - kVisibleOctaves);
    int page_start = 12 * (window_octave + 1); // Cn MIDI note
    int page_end = page_start + (12 * kVisibleOctaves) - 1;
    const int focus_n = static_cast<int>(focus_note);
    if(focus_n < page_start)
    {
        window_octave = KeyzonePageFromNote(focus_note);
        window_octave = ClampInt(window_octave, 0, kPageCount - kVisibleOctaves);
        page_start = 12 * (window_octave + 1);
        page_end = page_start + (12 * kVisibleOctaves) - 1;
    }
    else if(focus_n > page_end)
    {
        window_octave = KeyzonePageFromNote(focus_note) - (kVisibleOctaves - 1);
        window_octave = ClampInt(window_octave, 0, kPageCount - kVisibleOctaves);
        page_start = 12 * (window_octave + 1);
        page_end = page_start + (12 * kVisibleOctaves) - 1;
    }
    app.perform_keyzone_window_octave[layer & 1u] = static_cast<uint8_t>(window_octave);
    auto white_idx_from_semitone_in_oct = [](int semi_in_oct) -> int
    {
        switch(semi_in_oct)
        {
            case 0: return 0;
            case 2: return 1;
            case 4: return 2;
            case 5: return 3;
            case 7: return 4;
            case 9: return 5;
            case 11: return 6;
            default: return -1;
        }
    };
    auto black_after_white_from_semitone = [](int semi_in_oct) -> int
    {
        switch(semi_in_oct)
        {
            case 1: return 0;
            case 3: return 1;
            case 6: return 3;
            case 8: return 4;
            case 10: return 5;
            default: return -1;
        }
    };
    auto is_black = [](int semi_in_oct) -> bool
    {
        return semi_in_oct == 1 || semi_in_oct == 3 || semi_in_oct == 6 || semi_in_oct == 8
               || semi_in_oct == 10;
    };
    auto semitone_in_page = [&](uint8_t note) -> int
    {
        int semitone = static_cast<int>(note) - page_start;
        if(semitone < 0)
            semitone = 0;
        if(semitone > (12 * kVisibleOctaves) - 1)
            semitone = (12 * kVisibleOctaves) - 1;
        return semitone;
    };
    auto note_in_window = [&](uint8_t note) -> bool
    {
        const int n = static_cast<int>(note);
        return n >= page_start && n <= page_end;
    };
    auto key_rect_for_note = [&](uint8_t note, int& rx0, int& ry0, int& rx1, int& ry1)
    {
        const int semitone = semitone_in_page(note);
        const int oct = semitone / 12;
        const int semi_in_oct = semitone % 12;
        if(is_black(semi_in_oct))
        {
            const int after_white = black_after_white_from_semitone(semi_in_oct) + (oct * 7);
            const int center_x = kb_x0 + ((after_white + 1) * kWhiteW);
            rx0 = center_x - (kBlackW / 2);
            rx1 = rx0 + kBlackW - 1;
            ry0 = kb_y0;
            ry1 = kb_y0 + kBlackH - 1;
            return;
        }
        const int white_idx = white_idx_from_semitone_in_oct(semi_in_oct) + (oct * 7);
        rx0 = kb_x0 + (white_idx * kWhiteW);
        rx1 = rx0 + kWhiteW - 1;
        ry0 = kb_y0;
        ry1 = kb_y1;
    };
    auto draw_white_arrow_marker = [&](int white_idx, bool left_arrow)
    {
        const int x0 = kb_x0 + (white_idx * kWhiteW);
        const int x1 = x0 + kWhiteW - 1;
        const int y_cut = kb_y0 + kBlackH - 1;
        if(x1 - x0 + 1 < 5)
            return;

        const int marker_h = 6;
        const int marker_y0 = y_cut + 1 + ((kb_y1 - (y_cut + 1) + 1 - marker_h) / 2);
        const int marker_y1 = marker_y0 + marker_h - 1;
        if(marker_y0 < y_cut + 1 || marker_y1 > kb_y1)
            return;

        // Keep the arrow centered in the white key for both LO and HI markers.
        const int cx = (x0 + x1) / 2;
        constexpr int kArrowShiftRight = 1;
        if(left_arrow)
        {
            const int stem_x = ClampInt(cx + 1 + kArrowShiftRight, x0 + 3, x1 - 1);
            d.DrawRect(stem_x, marker_y0, stem_x, marker_y1, false, true); // 1x6 stem
            d.DrawRect(stem_x - 1, marker_y0 + 1, stem_x - 1, marker_y1 - 1, false, true); // 1x4
            d.DrawRect(stem_x - 2, marker_y0 + 2, stem_x - 2, marker_y1 - 2, false, true); // 1x2
        }
        else
        {
            const int stem_x = ClampInt(cx - 1 + kArrowShiftRight, x0 + 1, x1 - 3);
            d.DrawRect(stem_x, marker_y0, stem_x, marker_y1, false, true); // 1x6 stem
            d.DrawRect(stem_x + 1, marker_y0 + 1, stem_x + 1, marker_y1 - 1, false, true); // 1x4
            d.DrawRect(stem_x + 2, marker_y0 + 2, stem_x + 2, marker_y1 - 2, false, true); // 1x2
        }
    };

    auto highlight_note = [&](uint8_t note, bool is_lo_endpoint)
    {
        const int active_semitone = semitone_in_page(note);
        const int oct = active_semitone / 12;
        const int semi_in_oct = active_semitone % 12;
        if(is_black(semi_in_oct))
        {
            int arx0 = 0, ary0 = 0, arx1 = 0, ary1 = 0;
            key_rect_for_note(note, arx0, ary0, arx1, ary1);
            // Selected black key: inversion only (no dotted barrier).
            d.DrawRect(arx0, ary0, arx1, ary1, true, true);
            d.DrawRect(arx0, ary0, arx1, ary1, false, false);
        }
        else
        {
            const int white_idx = white_idx_from_semitone_in_oct(semi_in_oct) + (oct * 7);
            // White-key selection: directional marker only.
            draw_white_arrow_marker(white_idx, is_lo_endpoint);
        }
    };

    const bool show_lo = note_in_window(lo_note);
    const bool show_hi = note_in_window(hi_note);
    if(show_lo)
        highlight_note(lo_note, true);
    if(show_hi && (!show_lo || hi_note != lo_note))
        highlight_note(hi_note, false);

    if(highlight_both && !show_lo && !show_hi)
    {
        // In linked-shift mode, ensure some visual feedback by drawing focused endpoint if both are off-window.
        highlight_note(focus_note, focused_marker == 0u);
    }

}

void PerformKeyzone_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);

    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t layer = app.perform_layer & 1u;

    char header_label[16] = {};
    std::snprintf(header_label, sizeof(header_label), "kyzn %c", layer == 0 ? 'a' : 'b');
    const int header_w = MicroStringWidth(header_label);
    const int box_w = header_w + 4;
    const int box_h = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash
        = static_cast<int32_t>(app.engine_header_invert_until_ms - ctx.now_ms) > 0;
    if(header_invert_flash)
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, false, true);
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, false);
        DrawMicroString(d, header_label, box_x + 2, 2, true);
    }
    else
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, true);
        DrawMicroString(d, header_label, box_x + 2, 2, false);
    }

    constexpr int kSectionH = 16;
    char lo_note[8] = {};
    char hi_note[8] = {};
    char lo_text[16] = {};
    char hi_text[16] = {};
    FormatMidiNoteName(app.perform_keyzone_lo_note[layer], lo_note, sizeof(lo_note));
    FormatMidiNoteName(app.perform_keyzone_hi_note[layer], hi_note, sizeof(hi_note));
    std::snprintf(lo_text, sizeof(lo_text), "Lo:%s", lo_note);
    std::snprintf(hi_text, sizeof(hi_text), "Hi:%s", hi_note);

    auto draw_animated_dotted_box = [&](int x0, int y0, int x1, int y1)
    {
        const int phase = static_cast<int>((ctx.now_ms / 100u) & 1u);
        for(int x = x0; x <= x1; ++x)
        {
            if(((x + phase) & 1) == 0)
            {
                d.DrawPixel(x, y0, true);
                d.DrawPixel(x, y1, true);
            }
        }
        for(int y = y0; y <= y1; ++y)
        {
            if(((y + phase) & 1) == 0)
            {
                d.DrawPixel(x0, y, true);
                d.DrawPixel(x1, y, true);
            }
        }
    };

    const int status_y = (kSectionH - Font5x7::H) / 2;
    const int lo_w = TinyStringWidthCaseSensitiveTightColons(lo_text);
    const int hi_w = TinyStringWidthCaseSensitiveTightColons(hi_text);
    const int lo_x = 2;
    const int gap_w = 8;
    const int hi_x = lo_x + lo_w + gap_w;
    if(hi_x + hi_w <= box_x - 2)
    {
        const int box_pad_x = 2;
        const int box_top = status_y - 3;
        const int box_bottom = status_y + Font5x7::H + 1;
        const int lo_box_x0 = lo_x - box_pad_x;
        const int lo_box_x1 = lo_x + lo_w + box_pad_x;
        const int hi_box_x0 = hi_x - box_pad_x;
        const int hi_box_x1 = hi_x + hi_w + box_pad_x;
        if(ctx.rshift)
        {
            d.DrawRect(lo_box_x0 + 1, box_top + 1, lo_box_x1 - 1, box_bottom - 1, true, true);
            d.DrawRect(hi_box_x0 + 1, box_top + 1, hi_box_x1 - 1, box_bottom - 1, true, true);
            DrawTinyStringCaseSensitive(d, lo_text, lo_x, status_y, false);
            DrawTinyStringCaseSensitive(d, hi_text, hi_x, status_y, false);
        }
        else
        {
            DrawTinyStringCaseSensitive(d, lo_text, lo_x, status_y, true);
            DrawTinyStringCaseSensitive(d, hi_text, hi_x, status_y, true);
        }
        draw_animated_dotted_box(lo_box_x0, box_top, lo_box_x1, box_bottom);
        draw_animated_dotted_box(hi_box_x0, box_top, hi_box_x1, box_bottom);
    }

    DrawKeyzoneUiRangeTemplate(d, app, layer, ctx.rshift, 0, kSectionH);
}

void PerformAdsr_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);

    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t layer = app.perform_layer & 1u;
    const uint8_t adsr_row = PerformAdsrRow(app, layer);
    const Sample& sample = app.sd_slots[layer];
    const bool sample_loaded = (sample.pcm != nullptr && sample.length > 0);
    const SampleEdit* edit = sample_loaded ? &app.sd_edit_slots[layer] : nullptr;

    char header_label[16] = {};
    std::snprintf(header_label, sizeof(header_label), "adsr %c", layer == 0 ? 'a' : 'b');
    const int header_w = MicroStringWidth(header_label);
    const int box_w = header_w + 4;
    const int box_h = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash
        = static_cast<int32_t>(app.engine_header_invert_until_ms - ctx.now_ms) > 0;
    if(header_invert_flash)
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, false, true);
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, false);
        DrawMicroString(d, header_label, box_x + 2, 2, true);
    }
    else
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, true);
        DrawMicroString(d, header_label, box_x + 2, 2, false);
    }

    constexpr int kWaveX = 0;
    constexpr int kWaveY = 10;
    constexpr int kWaveW = 128;

    const char* kPlaybackTypeLabel = "playback type";
    const bool type_focused = app.perform_adsr_type_focus;
    const bool wave_focused = (!type_focused && app.perform_adsr_wave_focus
                               && PerformAdsrWaveFocusable(adsr_row));
    const int label_area_x0 = 0;
    const int label_area_x1 = box_x - 1;
    if(label_area_x1 >= label_area_x0)
    {
        const int label_area_w = label_area_x1 - label_area_x0 + 1;
        const int max_chars = (label_area_w + 1) / kMicroAdvance;
        if(max_chars > 0)
        {
            char clipped[24] = {};
            int i = 0;
            for(; kPlaybackTypeLabel[i] != '\0' && i < max_chars
                  && i + 1 < static_cast<int>(sizeof(clipped));
                ++i)
                clipped[i] = kPlaybackTypeLabel[i];
            clipped[i] = '\0';
            const int draw_w = MicroStringWidth(clipped);
            int draw_x = label_area_x0 + ((label_area_w - draw_w) / 2);
            if(draw_x < label_area_x0)
                draw_x = label_area_x0;
            int draw_y = (kWaveY - kMicroH) / 2;
            if(draw_y < 0)
                draw_y = 0;

            constexpr int kTypeFocusPadX = 5;
            constexpr int kTypeFocusPadY = 2;
            int rx0 = draw_x - kTypeFocusPadX;
            int ry0 = draw_y - kTypeFocusPadY;
            int rx1 = draw_x + draw_w + (kTypeFocusPadX - 1);
            int ry1 = draw_y + kMicroH + (kTypeFocusPadY - 1);
            if(rx0 < label_area_x0)
                rx0 = label_area_x0;
            if(rx1 > label_area_x1)
                rx1 = label_area_x1;
            if(ry0 < 0)
                ry0 = 0;
            if(ry1 > (kWaveY - 1))
                ry1 = kWaveY - 1;

            if(type_focused)
            {
                DrawDottedRect(d, rx0, ry0, rx1, ry1, true);
                const char* kTypeValues[kAdsrRowCount] = {"1shot", "loop", "adsr"};
                const char* selected = kTypeValues[adsr_row % static_cast<uint8_t>(kAdsrRowCount)];
                const int value_w = MiniString3x5Width(selected);
                int value_x = rx0 + (((rx1 - rx0 + 1) - value_w) / 2);
                int value_y = ry0 + (((ry1 - ry0 + 1) - kMini3x5H) / 2);
                if(value_x < label_area_x0)
                    value_x = label_area_x0;
                if(value_y < 0)
                    value_y = 0;
                if(value_x + value_w - 1 > label_area_x1)
                    value_x = label_area_x1 - value_w + 1;
                DrawMiniString3x5(d, selected, value_x, value_y, true);
            }
            else
            {
                DrawMicroString(d, clipped, draw_x, draw_y, true);
            }
        }
    }

    int kWaveBottomY = static_cast<int>(d.Height()) - 8;
    if(kWaveBottomY < kWaveY)
        kWaveBottomY = kWaveY;
    const int kWaveH = kWaveBottomY - kWaveY + 1;
    const bool adsr_mode
        = (adsr_row % static_cast<uint8_t>(kAdsrRowCount)) == static_cast<uint8_t>(kAdsrRowAdsr);

    DrawWaveformPreview(
        d, sample, edit, kWaveX, kWaveY, kWaveW, kWaveH, true, adsr_mode, wave_focused);

    static const char* kBottomLetters[4] = {"a", "d", "s", "r"};
    const int bottom_y = kWaveBottomY + 2;
    const int seg_w = static_cast<int>(d.Width()) / 4;
    const uint8_t stage_focus = app.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
    const bool loop_stage_editing
        = (adsr_row % static_cast<uint8_t>(kAdsrRowCount)) == static_cast<uint8_t>(kAdsrRowLoop);
    const int preview_x0 = kWaveX + 1;
    const int preview_x1 = kWaveX + kWaveW - 2;
    const int preview_y0 = kWaveY + 1;
    const int preview_y1 = kWaveBottomY - 1;
    const int preview_w = preview_x1 - preview_x0;
    const int preview_h = preview_y1 - preview_y0;

    if(wave_focused && sample_loaded && preview_x1 >= preview_x0 && preview_y1 >= preview_y0)
    {
        const int preview_pixels = preview_x1 - preview_x0 + 1;
        int crossfade_px = static_cast<int>(
            (static_cast<float>(preview_pixels - 1) * app.perform_adsr_loop_crossfade[layer]) + 0.5f);
        if(crossfade_px < 0)
            crossfade_px = 0;
        const int max_crossfade_px = (preview_pixels - 1) / 2;
        if(crossfade_px > max_crossfade_px)
            crossfade_px = max_crossfade_px;

        const int left_bar_x = preview_x0 + crossfade_px;
        const int right_bar_x = preview_x1 - crossfade_px;
        if(ctx.rshift)
        {
            for(int yy = preview_y0; yy <= preview_y1; ++yy)
            {
                for(int xx = preview_x0; xx <= left_bar_x; ++xx)
                {
                    if(((xx + yy) & 1) != 0)
                        d.DrawPixel(xx, yy, false);
                }
                for(int xx = right_bar_x; xx <= preview_x1; ++xx)
                {
                    if(((xx + yy) & 1) != 0)
                        d.DrawPixel(xx, yy, false);
                }
            }
            DrawPerformLoopCrossfadeCurve(d,
                                          preview_x0,
                                          preview_y0,
                                          left_bar_x,
                                          preview_y1,
                                          app.perform_adsr_loop_crossfade_shape[layer],
                                          false);
            DrawPerformLoopCrossfadeCurve(d,
                                          right_bar_x,
                                          preview_y0,
                                          preview_x1,
                                          preview_y1,
                                          app.perform_adsr_loop_crossfade_shape[layer],
                                          true);
            d.DrawRect(preview_x0, preview_y0, left_bar_x, preview_y1, true, false);
            d.DrawRect(right_bar_x, preview_y0, preview_x1, preview_y1, true, false);
        }
        else
        {
            for(int yy = preview_y0; yy <= preview_y1; ++yy)
            {
                for(int xx = preview_x0; xx < left_bar_x; ++xx)
                {
                    if(((xx + yy) & 1) == 0)
                        d.DrawPixel(xx, yy, true);
                }
                for(int xx = right_bar_x + 1; xx <= preview_x1; ++xx)
                {
                    if(((xx + yy) & 1) == 0)
                        d.DrawPixel(xx, yy, true);
                }
            }
        }
        d.DrawLine(left_bar_x, preview_y0, left_bar_x, preview_y1, true);
        d.DrawLine(right_bar_x, preview_y0, right_bar_x, preview_y1, true);
    }

    if(adsr_mode)
    {
        const int a_x = preview_x0 + (preview_w * static_cast<int>(app.perform_adsr_env_a_x[layer])) / 100;
        const int d_x = preview_x0 + (preview_w * static_cast<int>(app.perform_adsr_env_d_x[layer])) / 100;
        const int r_x = preview_x0 + (preview_w * static_cast<int>(app.perform_adsr_env_r_x[layer])) / 100;
        const int sustain_y
            = preview_y1 - (preview_h * static_cast<int>(app.perform_adsr_env_s_level[layer])) / 100;

        d.DrawLine(a_x, kWaveY + 1, a_x, kWaveBottomY - 1, true);
        d.DrawLine(d_x, kWaveY + 1, d_x, kWaveBottomY - 1, true);
        d.DrawLine(r_x, kWaveY + 1, r_x, kWaveBottomY - 1, true);

        d.DrawLine(preview_x0, preview_y1, a_x, preview_y0, true);
        d.DrawLine(a_x, preview_y0, d_x, sustain_y, true);
        d.DrawLine(d_x, sustain_y, r_x, sustain_y, true);
        d.DrawLine(r_x, sustain_y, preview_x1, preview_y1, true);
    }

    for(int i = 0; i < 4; ++i)
    {
        const int seg_start = i * seg_w;
        const int seg_end = (i == 3) ? (static_cast<int>(d.Width()) - 1) : ((i + 1) * seg_w - 1);
        const int seg_center = seg_start + ((seg_end - seg_start + 1) / 2);
        const bool stage_enabled = PerformAdsrStageEnabled(adsr_row, static_cast<uint8_t>(i));

        if(adsr_mode)
        {
            const int w = MiniString3x5Width(kBottomLetters[i]);
            int box_center = seg_center;
            if(i == 0)
                box_center = preview_x0 + (preview_w * static_cast<int>(app.perform_adsr_env_a_x[layer])) / 100;
            else if(i == 1)
                box_center = preview_x0 + (preview_w * static_cast<int>(app.perform_adsr_env_d_x[layer])) / 100;
            else if(i == 2)
            {
                const int d_x
                    = preview_x0 + (preview_w * static_cast<int>(app.perform_adsr_env_d_x[layer])) / 100;
                const int r_x
                    = preview_x0 + (preview_w * static_cast<int>(app.perform_adsr_env_r_x[layer])) / 100;
                box_center = d_x + ((r_x - d_x) / 2);
            }
            else
                box_center = preview_x0 + (preview_w * static_cast<int>(app.perform_adsr_env_r_x[layer])) / 100;
            const int box_w = w + 6;
            int box_x0 = box_center - (box_w / 2);
            int box_x1 = box_x0 + box_w - 1;
            if(box_x0 < seg_start)
            {
                box_x0 = seg_start;
                box_x1 = box_x0 + box_w - 1;
            }
            if(box_x1 > seg_end)
            {
                box_x1 = seg_end;
                box_x0 = box_x1 - box_w + 1;
            }

            const int box_y0 = bottom_y - 2;
            const int box_y1 = bottom_y + kMini3x5H + 1;
            const int value_x = box_x0 + ((box_w - w) / 2);
            const int value_y = bottom_y;
            if(!type_focused && stage_focus == static_cast<uint8_t>(i))
            {
                if(i == 2)
                {
                    d.DrawRect(box_x0, box_y0, box_x1, box_y1, true, true);
                    DrawMiniString3x5(d, kBottomLetters[i], value_x, value_y, false);
                }
                else
                {
                    DrawDottedRect(d, box_x0, box_y0, box_x1, box_y1, true);
                    DrawMiniString3x5(d, kBottomLetters[i], value_x, value_y, true);
                }
                continue;
            }
            DrawMiniString3x5(d, kBottomLetters[i], value_x, value_y, true);
            continue;
        }

        if(loop_stage_editing && !type_focused && stage_focus == static_cast<uint8_t>(i))
        {
            char value_buf[6] = {};
            const uint16_t value = PerformAdsrStageValue(app, layer, static_cast<uint8_t>(i));
            std::snprintf(value_buf, sizeof(value_buf), "%u", static_cast<unsigned>(value));
            const int value_w = MiniString3x5Width(value_buf);
            const int box_w = value_w + 6;
            int box_x0 = seg_center - (box_w / 2);
            int box_x1 = box_x0 + box_w - 1;
            if(box_x0 < seg_start)
            {
                box_x0 = seg_start;
                box_x1 = box_x0 + box_w - 1;
            }
            if(box_x1 > seg_end)
            {
                box_x1 = seg_end;
                box_x0 = box_x1 - box_w + 1;
            }

            const int box_y0 = bottom_y - 2;
            const int box_y1 = bottom_y + kMini3x5H + 1;
            const int value_x = box_x0 + ((box_w - value_w) / 2);
            const int value_y = bottom_y;
            DrawDottedRect(d, box_x0, box_y0, box_x1, box_y1, true);
            DrawMiniString3x5(d, value_buf, value_x, value_y, true);
        }
        else
        {
            const int w = MiniString3x5Width(kBottomLetters[i]);
            int x = seg_center - (w / 2);
            if(x < seg_start)
                x = seg_start;
            if(x + w - 1 > seg_end)
                x = seg_end - w + 1;
            if(stage_enabled && !type_focused && stage_focus == static_cast<uint8_t>(i))
            {
                d.DrawRect(x - 2, bottom_y - 1, x + w + 1, bottom_y + kMini3x5H, true, true);
                DrawMiniString3x5(d, kBottomLetters[i], x, bottom_y, false);
                continue;
            }
            DrawMiniString3x5(d, kBottomLetters[i], x, bottom_y, true);
            if(!stage_enabled)
            {
                int line_x0 = x - 1;
                int line_x1 = x + w;
                if(line_x0 < seg_start)
                    line_x0 = seg_start;
                if(line_x1 > seg_end)
                    line_x1 = seg_end;
                d.DrawLine(line_x0, bottom_y + 2, line_x1, bottom_y + 2, true);
            }
        }
    }
}

void PerformEmphasis_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display || !ctx.params)
        return;

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);

    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t layer = app.perform_layer & 1u;
    const PerformParamsTargets& t = ctx.params->TargetsForUI();
    const float cutoff_hz = t.engine_filter_cutoff_hz[layer];
    const float resonance = Clamp01(t.engine_filter_resonance[layer]);
    char header_label[16] = {};
    std::snprintf(header_label, sizeof(header_label), "emph %c", layer == 0 ? 'a' : 'b');
    const int header_w = MicroStringWidth(header_label);
    const int box_w = header_w + 4;
    const int box_h = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash
        = static_cast<int32_t>(app.engine_header_invert_until_ms - ctx.now_ms) > 0;
    if(header_invert_flash)
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, false, true);
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, false);
        DrawMicroString(d, header_label, box_x + 2, 2, true);
    }
    else
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, true);
        DrawMicroString(d, header_label, box_x + 2, 2, false);
    }

    auto clamp01f = [](float v) -> float
    {
        if(v < 0.0f) return 0.0f;
        if(v > 1.0f) return 1.0f;
        return v;
    };

    auto draw_knob = [&](int cx,
                         int cy,
                         int radius,
                         const char* label,
                         const char* value_text,
                         float angle_rad,
                         int focus_style)
    {
        DrawCirclePixels(d, cx, cy, radius, true);
        d.DrawPixel(cx, cy, true);
        const int hand_r = radius - 2;
        const int hx = cx + static_cast<int>(std::cos(angle_rad) * static_cast<float>(hand_r));
        const int hy = cy + static_cast<int>(std::sin(angle_rad) * static_cast<float>(hand_r));
        d.DrawLine(cx, cy, hx, hy, true);

        const int label_w = TinyStringWidth(label);
        const int label_x = cx - (label_w / 2);
        const int label_y = cy + radius + 5;
        if(focus_style == 1)
        {
            d.DrawRect(label_x - 2, label_y - 1, label_x + label_w + 1, label_y + Font5x7::H, true, false);
            DrawTinyString(d, label, label_x, label_y, true);
        }
        else if(focus_style == 2)
        {
            DrawDottedRect(d, label_x - 2, label_y - 1, label_x + label_w + 1, label_y + Font5x7::H, true);
            DrawTinyString(d, label, label_x, label_y, true);
        }
        else
        {
            DrawTinyString(d, label, label_x, label_y, true);
        }
        if(value_text && value_text[0] != '\0')
        {
            const int value_w = TinyStringWidth(value_text);
            DrawTinyString(d, value_text, cx - (value_w / 2), cy - radius - 8, true);
        }
    };

    char gain_buf[12];
    FormatDbTenths(app.engine_gain_db[layer], gain_buf, sizeof(gain_buf));
    const bool drive_mode_focus = (app.perform_emphasis_row == 0u) && ctx.rshift;
    const char* drive_label = drive_mode_focus ? DriveModeLabel(app.engine_drive_mode[layer]) : "drive";
    const char* drive_value = drive_mode_focus ? "" : gain_buf;

    float cutoff = cutoff_hz;
    if(cutoff < 20.0f) cutoff = 20.0f;
    if(cutoff > 20000.0f) cutoff = 20000.0f;
    char cutoff_buf[16];
    const uint32_t lpf_hz = static_cast<uint32_t>(cutoff + 0.5f);
    if(lpf_hz >= 1000u)
    {
        if((lpf_hz % 1000u) == 0u)
            std::snprintf(cutoff_buf, sizeof(cutoff_buf), "%luk", (unsigned long)(lpf_hz / 1000u));
        else
            std::snprintf(cutoff_buf,
                          sizeof(cutoff_buf),
                          "%lu.%01luk",
                          (unsigned long)(lpf_hz / 1000u),
                          (unsigned long)((lpf_hz % 1000u) / 100u));
    }
    else
        std::snprintf(cutoff_buf, sizeof(cutoff_buf), "%lu", (unsigned long)lpf_hz);

    const int gain_tenths = static_cast<int>(app.engine_gain_db[layer]);
    const float gain_norm = clamp01f(static_cast<float>(gain_tenths) / 60.0f);
    const float gain_angle = 2.0943951f + (gain_norm * 5.2359878f);

    const float cutoff_norm = AdsrFltFaderFromCutoffHz(cutoff);
    const float cutoff_angle = 2.0943951f + (cutoff_norm * 5.2359878f);
    const float reso_angle = 2.0943951f + (resonance * 5.2359878f);

    constexpr int kKnobRadius = 12;
    constexpr int kKnobCy = 28;
    constexpr int kKnobCx[3] = {22, 64, 106};
    draw_knob(kKnobCx[0],
              kKnobCy,
              kKnobRadius,
              drive_label,
              drive_value,
              gain_angle,
              app.perform_emphasis_row == 0 ? (drive_mode_focus ? 2 : 1) : 0);
    draw_knob(kKnobCx[1],
              kKnobCy,
              kKnobRadius,
              "cutoff",
              cutoff_buf,
              cutoff_angle,
              app.perform_emphasis_row == 1 ? 2 : 0);
    draw_knob(kKnobCx[2],
              kKnobCy,
              kKnobRadius,
              "reso",
              "",
              reso_angle,
              app.perform_emphasis_row == 2 ? 2 : 0);
}

bool PerformProcess_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
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
            case 1: return 1; // EQ: graph only (no classic detail)
            case 2: return 4; // DELAY: LTM RTM FBK MIX
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
       && (app.perform_process_detail_active || app.perform_process_eq_graph_active))
    {
        app.perform_process_detail_active   = false;
        app.perform_process_eq_graph_active = false;
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(app.perform_process_eq_graph_active)
        {
            app.ui_dirty = true;
            return true;
        }

        if(!app.perform_process_detail_active)
        {
            if(!main_selects_fx)
            {
                const uint8_t layer = main_cursor & 1u; // 0=VOL A, 1=VOL B
                PerformParamsTargets& t = ctx.params->EditTargets();
                if(ctx.rshift)
                {
                    // RSHIFT + click => snap to UNITY.
                    t.engine_layer_master_level[layer] = 1.0f;
                    app.perform_process_vol_unmuted_level[layer] = 1.0f;
                    app.perform_process_vol_muted[layer] = false;
                    app.perform_process_vol_pct[layer] = 100u;
                }
                else
                {
                    // Click => mute toggle for selected voice.
                    if(!app.perform_process_vol_muted[layer])
                    {
                        float saved = t.engine_layer_master_level[layer];
                        if(saved < 0.001f)
                            saved = 1.0f;
                        app.perform_process_vol_unmuted_level[layer] = saved;
                        app.perform_process_vol_muted[layer] = true;
                        t.engine_layer_master_level[layer] = 0.0f;
                        app.perform_process_vol_pct[layer] = 0u;
                    }
                    else
                    {
                        float restore = app.perform_process_vol_unmuted_level[layer];
                        if(restore < 0.0f) restore = 0.0f;
                        if(restore > 2.0f) restore = 2.0f;
                        app.perform_process_vol_muted[layer] = false;
                        t.engine_layer_master_level[layer] = restore;
                        app.perform_process_vol_pct[layer]
                            = static_cast<uint16_t>(restore * 100.0f + 0.5f);
                    }
                }
                ctx.params->PublishTargets();
                app.ui_dirty = true;
                return true;
            }
            {
                const uint8_t c   = static_cast<uint8_t>((app.perform_process_main_cursor - 2u) & 0x03u);
                const uint8_t fid = app.perform_process_fx_order[c];
                if(fid == 1u)
                {
                    app.perform_process_eq_graph_active = true;
                }
                else
                {
                    app.perform_process_detail_active = true;
                    if(fid == 2u && app.perform_process_detail_param[c] > 3u)
                        app.perform_process_detail_param[c] = 0u;
                }
            }
            app.ui_dirty = true;
            return true;
        }

        // In ADSR-style FX detail, toggles change via encoder scroll, not click.
        app.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        if(app.perform_process_eq_graph_active && fx_id == 1u)
        {
            PerformParamsTargets& t = ctx.params->EditTargets();
            const float           step = 0.018f * static_cast<float>(e.value);
            t.eq_center_norm           = Clamp01(t.eq_center_norm + step);
            ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }
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
        static uint32_t s_last_ext_t_ms = 0u;
        const float delta = UiDeltaNormAccelerated(e.value, e.t_ms, s_last_ext_t_ms, 0.02f);
        if(app.perform_process_eq_graph_active && fx_id == 1u)
        {
            PerformParamsTargets& t = ctx.params->EditTargets();
            if(ctx.rshift)
            {
                // RShift + Ext: Q for both bells (0.5 .. 1.7)
                t.eq_q = ClampEqQ(t.eq_q + delta * (kTiltEqQMax - kTiltEqQMin) * 1.25f);
            }
            else
            {
                t.eq_tilt_db = ClampEqTiltDb(t.eq_tilt_db + delta * 18.0f);
            }
            ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }
        if(app.perform_process_detail_active)
        {
            PerformParamsTargets& t = ctx.params->EditTargets();
            const uint8_t pidx = app.perform_process_detail_param[cursor];
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
                case 2: // DELAY
                    if(pidx == 0)
                    {
                        if(ctx.rshift)
                        {
                            t.delay_time_l = Clamp01(t.delay_time_l + delta);
                            t.delay_time_r = Clamp01(t.delay_time_r + delta);
                        }
                        else
                            t.delay_time_l = Clamp01(t.delay_time_l + delta);
                        changed = true;
                    }
                    else if(pidx == 1)
                    {
                        if(ctx.rshift)
                        {
                            t.delay_time_l = Clamp01(t.delay_time_l + delta);
                            t.delay_time_r = Clamp01(t.delay_time_r + delta);
                        }
                        else
                            t.delay_time_r = Clamp01(t.delay_time_r + delta);
                        changed = true;
                    }
                    else if(pidx == 2)
                    {
                        t.delay_feedback = Clamp01(t.delay_feedback + delta);
                        changed = true;
                    }
                    else if(pidx == 3)
                    {
                        t.delay_mix = Clamp01(t.delay_mix + delta);
                        t.delay_on = (t.delay_mix > 0.001f);
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
                    else if(pidx == 3)
                    {
                        t.reverb_mod = Clamp01(t.reverb_mod + delta);
                        changed = true;
                    }
                    else if(pidx == 4)
                    {
                        t.reverb_mix = Clamp01(t.reverb_mix + delta);
                        t.reverb_on = (t.reverb_mix > 0.001f);
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
            // RSHIFT + R encoder reorders focused S/E/D/R lane, clamped at edges.
            const int dir = (e.value > 0) ? 1 : -1;
            const int from = static_cast<int>(main_cursor - 2u);
            const int to = from + dir;
            if(to < 0 || to > 3)
            {
                app.ui_dirty = true;
                return true;
            }
            const uint8_t tmp = app.perform_process_fx_order[from];
            app.perform_process_fx_order[from] = app.perform_process_fx_order[to];
            app.perform_process_fx_order[to] = tmp;
            app.perform_process_fx_cursor = static_cast<uint8_t>(to);
            app.perform_process_main_cursor = static_cast<uint8_t>(to + 2);

            PerformParamsTargets& t = ctx.params->EditTargets();
            for(int i = 0; i < 4; ++i)
                t.fx_order[i] = app.perform_process_fx_order[i];
            ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }

        if(!main_selects_fx)
        {
            const uint8_t layer = main_cursor & 1u;
            PerformParamsTargets& t = ctx.params->EditTargets();
            if(app.perform_process_vol_muted[layer])
            {
                // Do not unmute on encoder turn; mute state toggles only on R-click.
                app.ui_dirty = true;
                return true;
            }

            // Match SETTINGS volume acceleration + range exactly.
            static uint32_t s_last_t_ms = 0;
            const uint32_t now_ms = e.t_ms;
            const uint32_t dt_ms  = (s_last_t_ms == 0) ? 999u : (now_ms - s_last_t_ms);
            s_last_t_ms = now_ms;

            float accel = 1.0f;
            if(dt_ms <= 25)       accel = 10.0f;
            else if(dt_ms <= 50)  accel = 6.0f;
            else if(dt_ms <= 90)  accel = 3.0f;
            else if(dt_ms <= 140) accel = 2.0f;

            const float base_step = 0.01f;
            float next = t.engine_layer_master_level[layer] + static_cast<float>(e.value) * base_step * accel;
            if(next < 0.0f) next = 0.0f;
            if(next > 2.0f) next = 2.0f;
            t.engine_layer_master_level[layer] = next;
            app.perform_process_vol_unmuted_level[layer] = next;
            app.perform_process_vol_pct[layer] = static_cast<uint16_t>(next * 100.0f + 0.5f);
            ctx.params->PublishTargets();
            app.ui_dirty = true;
            return true;
        }

        PerformParamsTargets& t = ctx.params->EditTargets();

        switch(fx_id)
        {
            case 0: // S = saturation drive
                t.sat_drive = Clamp01(t.sat_drive + delta);
                t.sat_on = (t.sat_drive > 0.001f);
                break;
            case 1: // E = EQ wet
                t.eq_mix = Clamp01(t.eq_mix + delta);
                t.eq_on  = (t.eq_mix > 0.001f);
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
                               uint32_t now_ms,
                               bool rshift_held)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;
    constexpr int kPerformFaderCount = 4;
    constexpr int kDelayFaderCount = 4;
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

    const char* labels[kPerformFaderCount] = {"SATURATION", "EQ", "DELAY", "REVERB"};
    int index = static_cast<int>(fx_id & 0x03u);
    if(index < 0 || index >= kPerformFaderCount)
        index = 0;

    if(index == 2)
    {
        const char*   hdr       = "delay";
        const int     header_w = MicroStringWidth(hdr);
        const int     box_w    = header_w + 4;
        const int     header_box_h = kMicroH + 4;
        int           box_x    = kDisplayW - box_w;
        if(box_x < 0)
            box_x = 0;
        d.DrawRect(box_x, 0, box_x + box_w - 1, header_box_h - 1, true, true);
        DrawMicroString(d, hdr, box_x + 2, 2, false);
    }
    else
    {
        const char* label   = labels[index];
        const int   text_w  = TinyStringWidth(label);
        int         text_x  = (kDisplayW - text_w) / 2;
        if(text_x < 0)
            text_x = 0;
        DrawTinyString(d, label, text_x, 1, true);
    }

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
        // EQ uses the graph submenu (Ext click); classic detail is not used for fx_id 1.
        const char* msg = "EXT: graph";
        const int   mw   = TinyStringWidth(msg);
        int         mx   = (kDisplayW - mw) / 2;
        if(mx < 0)
            mx = 0;
        DrawTinyString(d, msg, mx, kDisplayH / 2 - Font5x7::H, true);
    }
    else if(index == 2)
    {
        constexpr int kMargin = 2;
        const int     header_box_h = kMicroH + 4;
        const int     block_y    = header_box_h;
        int           block_h    = kDisplayH - block_y - kMargin;
        if(block_h < 3)
            block_h = 3;
        const int fader_x = kMargin;
        const int fader_w = kDisplayW - (kMargin * 2);
        if(fader_w > 4)
        {
            const char* fader_labels[kDelayFaderCount] = {"LTM", "RTM", "FBK", "MIX"};
            const float fader_values[kDelayFaderCount]
                = {t.delay_time_l, t.delay_time_r, t.delay_feedback, t.delay_mix};
            int param_index = selected_param;
            const bool fader_select_active = (param_index >= 0 && param_index < kDelayFaderCount);
            if(!fader_select_active)
                param_index = 0;
            int lb_x0[4], lb_y0[4], lb_x1[4], lb_y1[4];
            DrawDelayDetailFaders(d, fader_x, block_y, fader_w, block_h, fader_labels, fader_values,
                                fader_select_active, param_index, lb_x0, lb_y0, lb_x1, lb_y1);

            if(rshift_held)
            {
                DrawClockwiseMarchingDottedRect(d, lb_x0[0] - 1, lb_y0[0] - 1, lb_x1[0] + 1, lb_y1[0] + 1, now_ms);
                DrawClockwiseMarchingDottedRect(d, lb_x0[1] - 1, lb_y0[1] - 1, lb_x1[1] + 1, lb_y1[1] + 1, now_ms);
            }
            else
            {
                if(fader_select_active && param_index == 0)
                    DrawDottedRect(d, lb_x0[0] - 1, lb_y0[0] - 1, lb_x1[0] + 1, lb_y1[0] + 1, true);
                if(fader_select_active && param_index == 1)
                    DrawDottedRect(d, lb_x0[1] - 1, lb_y0[1] - 1, lb_x1[1] + 1, lb_y1[1] + 1, true);
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
            const char* fader_labels[kReverbFaderCount] = {"Pre", "Dmp", "Dcy", "Mod", "Wet"};
            const float fader_values[kReverbFaderCount] = {t.reverb_pre, t.reverb_damp, t.reverb_decay, t.reverb_mod, t.reverb_mix};
            int param_index = selected_param;
            const bool fader_select_active = (param_index >= 0 && param_index < kReverbFaderCount);
            if(!fader_select_active) param_index = 0;
            const bool hide_handles[kReverbFaderCount] = {false, false, false, false, false};
            const bool hide_rails[kReverbFaderCount] = {false, false, false, false, false};
            const int fader_offsets[kReverbFaderCount] = {0, 1, -1, 0, 0};
            DrawVerticalFadersInRect(d, fader_x, block_y, fader_w, block_h,
                                     fader_labels, fader_values, kReverbFaderCount, fader_select_active, param_index,
                                     fader_offsets, nullptr, hide_rails, hide_handles);
        }
    }
}

static void DrawEqGraphScreen(OledPager& d, const PerformParamsTargets& t, uint32_t now_ms)
{
    constexpr int   kDisplayW = 128;
    constexpr int   kDisplayH = 64;
    constexpr float kPlotSr   = 48000.f;
    constexpr float kFMin     = 20.f;
    constexpr float kFMax     = 20000.f;

    d.Fill(false);

    const int plot_x0 = 2;
    const int plot_x1 = kDisplayW - 3;
    const int plot_y0 = 9;
    const int plot_y1 = kDisplayH - 11;

    const char* hdr = "eq";
    const int   hw  = MicroStringWidth(hdr);
    int         hx  = (kDisplayW - hw) / 2;
    if(hx < 0)
        hx = 0;
    DrawMicroString(d, hdr, hx, 0, true);

    const float center_hz = TiltEq_CenterNormToHz(t.eq_center_norm);
    const float tilt      = ClampEqTiltDb(t.eq_tilt_db);
    const float kLogSpan = std::log10(static_cast<double>(kFMax / kFMin));

    static uint32_t s_last_curve_ms = 0u;
    static float    s_prev_c        = -999.f;
    static float    s_prev_t        = -999.f;
    static float    s_prev_q        = -999.f;
    static int16_t  s_y[128];

    const float eq_q = ClampEqQ(t.eq_q);
    const bool recompute = (now_ms - s_last_curve_ms >= 72u)
                           || (std::fabs(t.eq_center_norm - s_prev_c) > 0.0005f)
                           || (std::fabs(t.eq_tilt_db - s_prev_t) > 0.02f)
                           || (std::fabs(eq_q - s_prev_q) > 0.015f);

    if(recompute)
    {
        for(int px = plot_x0; px <= plot_x1; ++px)
        {
            const float tn = (static_cast<float>(px - plot_x0))
                             / static_cast<float>(plot_x1 - plot_x0);
            const float f  = kFMin * std::pow(10.f, tn * kLogSpan);
            float       db = TiltEq_CascadeMagnitudeDb(center_hz, tilt, f, kPlotSr, eq_q);
            if(db > 9.f)
                db = 9.f;
            if(db < -9.f)
                db = -9.f;
            const float yn = static_cast<float>(plot_y0)
                             + (9.f - db) / 18.f * static_cast<float>(plot_y1 - plot_y0);
            int         y  = static_cast<int>(yn + 0.5f);
            if(y < plot_y0)
                y = plot_y0;
            if(y > plot_y1)
                y = plot_y1;
            s_y[px] = static_cast<int16_t>(y);
        }
        s_last_curve_ms = now_ms;
        s_prev_c        = t.eq_center_norm;
        s_prev_t        = t.eq_tilt_db;
        s_prev_q        = eq_q;
    }

    for(int px = plot_x0; px < plot_x1; ++px)
        d.DrawLine(px, s_y[px], px + 1, s_y[px + 1], true);

    auto hz_to_x = [&](float hz) -> int
    {
        const float lg = static_cast<float>(std::log10(static_cast<double>(hz / kFMin))) / kLogSpan;
        return plot_x0
               + static_cast<int>(lg * static_cast<float>(plot_x1 - plot_x0) + 0.5f);
    };

    // Tiny font: micro glyph set has no reliable digits (narrow "1" vanishes in 5x7->4x6 crop).
    const int lab_y = kDisplayH - Font5x7::H;
    auto draw_hz_label = [&](const char* s, float hz)
    {
        const int x   = hz_to_x(hz);
        const int w   = TinyStringWidth(s);
        const int lx  = x - w / 2;
        DrawTinyString(d, s, lx, lab_y, true);
    };
    draw_hz_label("100", 100.f);
    draw_hz_label("1k", 1000.f);
    draw_hz_label("10k", 10000.f);
}

void PerformProcess_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display || !ctx.params)
        return;

    AppState& app = *ctx.app;
    EngineRefreshLoadedMetadata(app);

    OledPager& d = *ctx.display;
    d.Fill(false);

    if(app.perform_process_eq_graph_active)
    {
        const uint8_t cursor = app.perform_process_fx_cursor & 0x03u;
        const uint8_t g_fx = app.perform_process_fx_order[cursor];
        if(g_fx == 1u)
        {
            const PerformParamsTargets& tg = ctx.params->TargetsForUI();
            DrawEqGraphScreen(d, tg, ctx.now_ms);
            return;
        }
        app.perform_process_eq_graph_active = false;
    }

    if(app.perform_process_detail_active)
    {
        const uint8_t cursor = app.perform_process_fx_cursor & 0x03u;
        const uint8_t fx_id = app.perform_process_fx_order[cursor];
        const uint8_t pidx = app.perform_process_detail_param[cursor];
        const PerformParamsTargets& t = ctx.params->TargetsForUI();
        DrawFxDetailScreen(d, t, fx_id, pidx, ctx.now_ms, ctx.rshift);
        return;
    }

    const UiLayout layout = UiLayout_Default();
    const char* header_label = "process";
    const int header_w = MicroStringWidth(header_label);
    const int box_w = header_w + 4;
    const int header_box_h = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash = static_cast<int32_t>(app.engine_header_invert_until_ms - ctx.now_ms) > 0;
    if(header_invert_flash)
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, header_box_h - 1, false, true);
        d.DrawRect(box_x, 0, box_x + box_w - 1, header_box_h - 1, true, false);
        DrawMicroString(d, header_label, box_x + 2, 2, true);
    }
    else
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, header_box_h - 1, true, true);
        DrawMicroString(d, header_label, box_x + 2, 2, false);
    }

    const uint8_t main_cursor = static_cast<uint8_t>(app.perform_process_main_cursor % 6u);
    const int32_t selected_index = (main_cursor >= 2u) ? static_cast<int32_t>(main_cursor - 2u) : -1;
    const int box_y = layout.y_body;
    const int box_h = layout.y_footer - layout.y_body + layout.line_h;
    const PerformParamsTargets& t = ctx.params->TargetsForUI();

    auto draw_process_knob = [&](int cx,
                                 int cy,
                                 int radius,
                                 char side_letter,
                                 const char* value_text,
                                 float angle_rad,
                                 bool focused)
    {
        DrawCirclePixels(d, cx, cy, radius, true);
        d.DrawPixel(cx, cy, true);
        const int hand_r = radius - 2;
        const int hx = cx + static_cast<int>(std::cos(angle_rad) * static_cast<float>(hand_r));
        const int hy = cy + static_cast<int>(std::sin(angle_rad) * static_cast<float>(hand_r));
        d.DrawLine(cx, cy, hx, hy, true);

        char side_text[2] = {side_letter, '\0'};
        const int label_w = TinyStringWidth(side_text);
        const int label_x = cx - radius - 9;
        const int label_y = cy - (Font5x7::H / 2);
        if(focused)
        {
            d.DrawRect(label_x - 3, label_y - 2, label_x + label_w + 2, label_y + Font5x7::H + 1, true, false);
            DrawTinyString(d, side_text, label_x, label_y, true);
        }
        else
        {
            DrawTinyString(d, side_text, label_x, label_y, true);
        }

        if(value_text && value_text[0] != '\0')
        {
            auto process_value_advance = [](char ch, char next_ch) -> int
            {
                if(ch == '.')
                    return 3;
                if(next_ch == 'd')
                    return 6;
                if(ch == 'd' && next_ch == 'b')
                    return 6;
                if(next_ch == '.')
                    return 5;
                return 5;
            };

            auto process_value_width = [&](const char* s) -> int
            {
                if(s == nullptr || s[0] == '\0')
                    return 0;
                int width = 0;
                for(int i = 0; s[i] != '\0'; ++i)
                    width += process_value_advance(s[i], s[i + 1]);
                return width;
            };

            auto draw_process_value = [&](const char* s, int x, int y)
            {
                int pen_x = x;
                for(int i = 0; s[i] != '\0'; ++i)
                {
                    char ch = s[i];
                    if(ch >= 'A' && ch <= 'Z')
                        ch = static_cast<char>(ch - 'A' + 'a');

                    uint8_t rows[Font5x7::H] = {};
                    Font5x7::GetGlyphRows(ch, rows);
                    for(int yy = 0; yy < Font5x7::H; ++yy)
                    {
                        const uint8_t row = rows[yy];
                        for(int xx = 0; xx < Font5x7::W; ++xx)
                        {
                            if((row >> (Font5x7::W - 1 - xx)) & 1)
                            {
                                const int px = pen_x + xx - ((ch == '.') ? 1 : 0);
                                const int py = y + yy;
                                if(px >= 0 && px < 128 && py >= 0 && py < 64)
                                    d.DrawPixel(px, py, true);
                            }
                        }
                    }
                    pen_x += process_value_advance(ch, s[i + 1]);
                }
            };

            auto draw_plus = [&](int x, int y)
            {
                d.DrawLine(x + 2, y + 1, x + 2, y + 5, true);
                d.DrawLine(x, y + 3, x + 4, y + 3, true);
            };

            if(value_text[0] == '+')
            {
                const char* rest = value_text + 1;
                const int rest_w = process_value_width(rest);
                const int value_w = 5 + 1 + rest_w;
                const int value_x = cx - (value_w / 2);
                const int value_y = cy - radius - 8;
                draw_plus(value_x, value_y);
                draw_process_value(rest, value_x + 6, value_y);
            }
            else
            {
                const int value_w = process_value_width(value_text);
                draw_process_value(value_text, cx - (value_w / 2), cy - radius - 8);
            }
        }
    };

    // Left pane: stacked layer volume knobs.
    constexpr int kLeftX = 0;
    constexpr int kLeftW = 60;
    const int left_y = box_y;
    const int left_h = box_h;
    if(left_h > 24)
    {
        const bool sel_a = (main_cursor == 0u);
        const bool sel_b = (main_cursor == 1u);

        const uint32_t a_pct = static_cast<uint32_t>(t.engine_layer_master_level[0] * 100.0f + 0.5f);
        const uint32_t b_pct = static_cast<uint32_t>(t.engine_layer_master_level[1] * 100.0f + 0.5f);
        app.perform_process_vol_pct[0] = static_cast<uint16_t>(a_pct);
        app.perform_process_vol_pct[1] = static_cast<uint16_t>(b_pct);

        char a_buf[12];
        char b_buf[12];
        FormatProcessLevelDb(t.engine_layer_master_level[0], a_buf, sizeof(a_buf));
        FormatProcessLevelDb(t.engine_layer_master_level[1], b_buf, sizeof(b_buf));
        const float a_norm = ProcessLevelToKnobNorm(t.engine_layer_master_level[0]);
        const float b_norm = ProcessLevelToKnobNorm(t.engine_layer_master_level[1]);
        const float a_angle = 2.0943951f + (a_norm * 5.2359878f);
        const float b_angle = 2.0943951f + (b_norm * 5.2359878f);

        constexpr int kVolKnobRadius = 9;
        const int knob_cx = kLeftX + (kLeftW / 2) - 1;
        const int a_cy = left_y + 13;
        const int b_cy = left_y + left_h - 13;
        draw_process_knob(knob_cx, a_cy, kVolKnobRadius, 'a', a_buf, a_angle, sel_a);
        draw_process_knob(knob_cx, b_cy, kVolKnobRadius, 'b', b_buf, b_angle, sel_b);
    }

    // Keep right half for FX faders.
    constexpr int kPaneX = 60;
    constexpr int kPaneW = 64;
    const char* labels[4] = {"S", "E", "D", "R"};
    float values[4] = {};
    for(int i = 0; i < 4; ++i)
    {
        const uint8_t fx_id = app.perform_process_fx_order[i];
        switch(fx_id)
        {
            case 0: labels[i] = "S"; values[i] = Clamp01(t.sat_drive); break;
            case 1: labels[i] = "E"; values[i] = Clamp01(t.eq_mix); break;
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
    {
        DrawVerticalFadersInRect(d,
                                 fader_x,
                                 fader_y,
                                 fader_w,
                                 fader_h,
                                 labels,
                                 values,
                                 4,
                                 true,
                                 selected_index,
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 1,
                                 1,
                                 1);

        if(ctx.rshift && selected_index >= 0 && selected_index < 4)
        {
            const int line_top = fader_y + 2;
            const int label_y = fader_y + fader_h - Font5x7::H - 1;
            const int line_bottom = label_y - 2;
            if(line_bottom > line_top)
            {
                const int fader_left = fader_x + 4;
                const int fader_right = fader_x + fader_w - 5;
                const int span_x = fader_right - fader_left;
                const int line_x = (span_x > 0)
                                       ? (fader_left + (span_x * selected_index) / 3)
                                       : fader_left;
                const int cy = line_top + ((line_bottom - line_top) / 2);
                const bool can_move_left = (selected_index > 0);
                const bool can_move_right = (selected_index < 3);

                auto draw_left_arrow = [&](int tip_x)
                {
                    d.DrawPixel(tip_x, cy, true);
                    d.DrawPixel(tip_x + 1, cy - 1, true);
                    d.DrawPixel(tip_x + 1, cy, true);
                    d.DrawPixel(tip_x + 1, cy + 1, true);
                    d.DrawPixel(tip_x + 2, cy - 2, true);
                    d.DrawPixel(tip_x + 2, cy - 1, true);
                    d.DrawPixel(tip_x + 2, cy, true);
                    d.DrawPixel(tip_x + 2, cy + 1, true);
                    d.DrawPixel(tip_x + 2, cy + 2, true);
                };

                auto draw_right_arrow = [&](int tip_x)
                {
                    d.DrawPixel(tip_x, cy, true);
                    d.DrawPixel(tip_x - 1, cy - 1, true);
                    d.DrawPixel(tip_x - 1, cy, true);
                    d.DrawPixel(tip_x - 1, cy + 1, true);
                    d.DrawPixel(tip_x - 2, cy - 2, true);
                    d.DrawPixel(tip_x - 2, cy - 1, true);
                    d.DrawPixel(tip_x - 2, cy, true);
                    d.DrawPixel(tip_x - 2, cy + 1, true);
                    d.DrawPixel(tip_x - 2, cy + 2, true);
                };

                if(can_move_left)
                    draw_left_arrow(line_x - 7);
                if(can_move_right)
                    draw_right_arrow(line_x + 7);
            }
        }
    }
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

bool Hud_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
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

void Hud_Render(UiScreenCtx& ctx)
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

    UiDraw_Footer(d, layout, "EXT:SEL EXT:ENT P2:BACK");
}

bool Fx_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
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

void Fx_Render(UiScreenCtx& ctx)
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

bool Mod_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
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

void Mod_Render(UiScreenCtx& ctx)
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

bool Macro_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
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

void Macro_Render(UiScreenCtx& ctx)
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

// -------------------------
// SHIFT MENU (POD BUTTON1)
// -------------------------

enum ShiftMenuItem : uint8_t
{
    ShiftDelete = 0,
    ShiftVolume,
    ShiftProjectSlot,
    ShiftSaveProject,
    ShiftLoadProject,
    ShiftCount
};

void ShiftMenu_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    // Returning to SHIFT should cancel any SD delete mode.
    ctx.app->sd_delete_mode = false;
    ctx.app->shift_menu_edit_volume = false;
}

bool ShiftMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
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

        if(!app.shift_menu_edit_volume
           && e.id == kUiEncExt
           && app.shift_menu_cursor == ShiftProjectSlot
           && e.value != 0)
        {
            const int next = static_cast<int>(app.current_project_slot) + e.value;
            app.current_project_slot = ProjectActions_WrapSlot(next);
            app.ui_dirty = true;
            return true;
        }

        // L encoder turn scrolls between settings rows when not editing.
        if(!app.shift_menu_edit_volume && e.id == kUiEncPod)
        {
            uint8_t cur = app.shift_menu_cursor;
            if(e.value > 0)
                cur = (cur + 1u < ShiftCount) ? static_cast<uint8_t>(cur + 1u) : cur;
            else if(e.value < 0)
                cur = (cur > 0u) ? static_cast<uint8_t>(cur - 1u) : cur;

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
        if(app.shift_menu_cursor == ShiftDelete)
        {
            // DELETE: enter SD browser in delete mode.
            app.sd_delete_mode = true;
            app.shift_menu_edit_volume = false;
            SdBrowser_SetStatus(app.sd, "DEL:SELECT");
            UiNav_Push(app.ui_nav, UiScreenId::SdBrowse);
            app.ui_dirty = true;
            return true;
        }
        if(app.shift_menu_cursor == ShiftVolume)
        {
            if(app.shift_menu_edit_volume && ctx.params)
            {
                auto& t = ctx.params->EditTargets();
                t.master_level = 1.0f; // UNITY
                ctx.params->PublishTargets();
                app.ui_dirty = true;
                return true;
            }
            // VOLUME: toggle edit mode.
            app.shift_menu_edit_volume = !app.shift_menu_edit_volume;
            app.ui_dirty = true;
            return true;
        }
        if(app.shift_menu_cursor == ShiftSaveProject)
            return ProjectActions_TriggerRequest(app,
                                                 UiReqType::SaveProject,
                                                 app.current_project_slot);
        if(app.shift_menu_cursor == ShiftLoadProject)
            return ProjectActions_TriggerRequest(app,
                                                 UiReqType::LoadProject,
                                                 app.current_project_slot);
        return true;
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

void ShiftMenu_Render(UiScreenCtx& ctx)
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

    // Settings rows: DELETE / OUTPUT VOL / PROJECT SLOT / SAVE PROJECT / LOAD PROJECT.
    const int row_y0 = layout.y_body;
    const int row_h = layout.line_h;

    for(int i = 0; i < ShiftCount; ++i)
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
        if(i == ShiftDelete)
        {
            d.WriteString("DELETE", Font_6x8, !sel);
        }
        else if(i == ShiftVolume)
        {
            const char* label = app.shift_menu_edit_volume ? "OUTPUT VOL*" : "OUTPUT VOL";
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
        else if(i == ShiftProjectSlot)
        {
            d.WriteString("PROJECT SLOT", Font_6x8, !sel);

            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02u", static_cast<unsigned>(app.current_project_slot + 1u));
            d.SetCursor(screen_w - 12 - 1, y + 1);
            d.WriteString(buf, Font_6x8, true);
        }
        else if(i == ShiftSaveProject)
        {
            d.WriteString("SAVE PROJECT", Font_6x8, !sel);
        }
        else if(i == ShiftLoadProject)
        {
            d.WriteString("LOAD PROJECT", Font_6x8, !sel);
        }
    }

    const char* hint = app.shift_menu_edit_volume ? "L:NAV R:CHG P2:BACK"
                        : (app.shift_menu_cursor == ShiftProjectSlot) ? "L:NAV R:CHG/CLK"
                                                                     : "L:NAV R:SEL";
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
                                            PerformWaveEdit_OnScreenEnter,
                                            nullptr,
                                            PerformWaveEdit_OnEvent,
                                            PerformWaveEdit_Render,
                                            PerformWaveEdit_OnEnter};
    static const UiScreen perform_keyzone{UiScreenId::PerformKeyzone, nullptr, nullptr, PerformKeyzone_OnEvent, PerformKeyzone_Render};
    static const UiScreen perform_adsr{UiScreenId::PerformAdsr,
                                       PerformAdsr_OnScreenEnter,
                                       nullptr,
                                       PerformAdsr_OnEvent,
                                       PerformAdsr_Render};
    static const UiScreen perform_emphasis{UiScreenId::PerformEmphasis,
                                           PerformEmphasis_OnScreenEnter,
                                           nullptr,
                                           PerformEmphasis_OnEvent,
                                           PerformEmphasis_Render};
    static const UiScreen perform_process{UiScreenId::PerformProcess, nullptr, nullptr, PerformProcess_OnEvent, PerformProcess_Render};
    static const UiScreen project_status{UiScreenId::ProjectStatus,
                                         nullptr,
                                         nullptr,
                                         ProjectStatus_OnEvent,
                                         ProjectStatus_Render};
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
        case UiScreenId::ProjectStatus:
            return project_status;
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

        // TRIM screen uses BACK to cancel pending start/end edits.
        if(active == UiScreenId::PerformWaveEdit && s.OnEvent && s.OnEvent(ctx, e))
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

    // Hold LShift to temporarily preview parent without changing nav state.
    if(ctx.lshift && ctx.app->ui_parent_preview_active)
    {
        if(ctx.app->ui_parent_preview_mode == 2
           && UiNav_Active(ctx.app->ui_nav) == UiScreenId::PerformProcess)
        {
            // In-screen parent: PROCESS detail / EQ graph -> PROCESS main.
            const bool saved_detail = ctx.app->perform_process_detail_active;
            const bool saved_eqg    = ctx.app->perform_process_eq_graph_active;
            ctx.app->perform_process_detail_active  = false;
            ctx.app->perform_process_eq_graph_active = false;
            const UiScreen& active = GetScreen(UiScreenId::PerformProcess);
            if(active.Render)
                active.Render(ctx);
            ctx.app->perform_process_detail_active   = saved_detail;
            ctx.app->perform_process_eq_graph_active = saved_eqg;
            return;
        }

        if(ctx.app->ui_parent_preview_mode == 1 && ctx.app->ui_nav.top > 0)
        {
            const UiScreenId parent_id = ctx.app->ui_nav.stack[ctx.app->ui_nav.top - 1];
            const UiScreen& parent = GetScreen(parent_id);
            if(parent.Render)
            {
                parent.Render(ctx);
                return;
            }
        }
    }

    const UiScreen& active = GetScreen(UiNav_Active(ctx.app->ui_nav));
    if(active.Render)
        active.Render(ctx);
}
