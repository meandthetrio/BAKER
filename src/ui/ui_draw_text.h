#pragma once

#include <cstddef>
#include <cstdint>

class OledPager;

struct Font5x7
{
    static constexpr int W = 5;
    static constexpr int H = 7;

    static void GetGlyphRows(char c, uint8_t out_rows[H]);
};

constexpr int kMini3x5W = 3;
constexpr int kMini3x5H = 5;
constexpr int kMini3x5Advance = kMini3x5W + 1;

constexpr int kMicroW = 4;
constexpr int kMicroH = 6;
constexpr int kMicroAdvance = kMicroW + 1;

void DrawScaledText6x8(OledPager& d, const char* text, int x, int y, int scale);
void DrawTinyString(OledPager& d, const char* str, int x, int y, bool on);
void DrawTinyStringCaseSensitive(OledPager& d, const char* str, int x, int y, bool on);
int TinyStringWidthCaseSensitiveTightColons(const char* str);
int TinyStringWidth(const char* str);
void DrawMiniString3x5(OledPager& d, const char* str, int x, int y, bool on);
int MiniString3x5Width(const char* str);
void DrawMicroString(OledPager& d, const char* str, int x, int y, bool on);
int MicroStringWidth(const char* str);
