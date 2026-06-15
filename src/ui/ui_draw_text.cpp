#include "ui_draw_text.h"

#include "oled_pager.h"

#include <initializer_list>

using namespace daisy;

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

void Font5x7::GetGlyphRows(char c, uint8_t out_rows[H])
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
        case ';': set({0b00000, 0b00100, 0b00000, 0b00000, 0b00100, 0b00100, 0b01000}); return;
        default: break;
    }

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
        case '#': set({0b01010, 0b11111, 0b01010, 0b11111, 0b01010, 0b01010, 0b00000}); return;
        case '?': set({0b01110, 0b10001, 0b00010, 0b00100, 0b00100, 0, 0b00100}); return;
        default: break;
    }

    set({0b11111, 0b10001, 0b00010, 0b00100, 0b00100, 0, 0b00100});
}

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

void DrawTinyStringCaseSensitive(OledPager& d, const char* str, int x, int y, bool on)
{
    const int char_w = Font5x7::W + 1;
    int pen_x = x;
    for(int i = 0; str[i] != '\0'; ++i)
    {
        const char ch = str[i];

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

int TinyStringWidthCaseSensitiveTightColons(const char* str)
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
        case 'i': out_rows[0] = 0b010; out_rows[1] = 0b000; out_rows[2] = 0b010; out_rows[3] = 0b010; out_rows[4] = 0b010; return;
        case 'k': out_rows[0] = 0b100; out_rows[1] = 0b101; out_rows[2] = 0b110; out_rows[3] = 0b101; out_rows[4] = 0b101; return;
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

void DrawMiniString3x5(OledPager& d, const char* str, int x, int y, bool on)
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

int MiniString3x5Width(const char* str)
{
    if(str == nullptr || str[0] == '\0')
        return 0;
    int count = 0;
    for(; str[count] != '\0'; ++count)
    {
    }
    return count * kMini3x5Advance - 1;
}

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
        case '?': set(0b0110, 0b1001, 0b0001, 0b0010, 0b0000, 0b0010); return;
        // Dedicated 4-wide chevrons. The Font5x7 fallback downsamples through
        // col_map {0,1,3,4}, which drops the center column where the chevron tip
        // sits — leaving a broken, gappy glyph. These draw a clean tip at the
        // x-height (rows 1..5) so they sit level with the adjacent lowercase.
        case '>': set(0b0000, 0b1000, 0b0100, 0b0010, 0b0100, 0b1000); return;
        case '<': set(0b0000, 0b0010, 0b0100, 0b1000, 0b0100, 0b0010); return;
        default: break;
    }

    uint8_t rows5[Font5x7::H] = {};
    Font5x7::GetGlyphRows(c, rows5);

    // 5x7 glyphs into 6 micro rows: use font rows 0..4 as-is, then fold font rows 5+6
    // into the last micro row (OR per column) so digits keep both the top curve and bottom.
    constexpr int col_map[kMicroW] = {0, 1, 3, 4};
    const bool      digit           = (c >= '0' && c <= '9');
    for(int yy = 0; yy < kMicroH; ++yy)
    {
        uint8_t bits = 0;
        for(int xx = 0; xx < kMicroW; ++xx)
        {
            const int src_col = col_map[xx];
            bool      on      = false;
            if(digit && yy == kMicroH - 1)
            {
                const uint8_t r5 = rows5[5];
                const uint8_t r6 = rows5[6];
                on = (((r5 >> (Font5x7::W - 1 - src_col)) & 1u) != 0)
                     || (((r6 >> (Font5x7::W - 1 - src_col)) & 1u) != 0);
            }
            else
            {
                const uint8_t src = rows5[yy];
                on                = ((src >> (Font5x7::W - 1 - src_col)) & 1u) != 0;
            }
            if(on)
                bits |= static_cast<uint8_t>(1u << (kMicroW - 1 - xx));
        }
        out_rows[yy] = bits;
    }
}

void DrawMicroString(OledPager& d, const char* str, int x, int y, bool on)
{
    if(str == nullptr)
        return;

    for(int i = 0; str[i] != '\0'; ++i)
    {
        if(str[i] == 'm')
        {
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

int MicroStringWidth(const char* str)
{
    if(str == nullptr || str[0] == '\0')
        return 0;
    int count = 0;
    for(; str[count] != '\0'; ++count)
    {
    }
    return count * kMicroAdvance - 1;
}
