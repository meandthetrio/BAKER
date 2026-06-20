#pragma once
// Host-only stand-in for the firmware OledPager so the REAL ui_draw_*.cpp
// (fonts, circles, faders) compile and run off-target. DrawPixel/DrawLine/
// DrawRect are copied VERBATIM from libDaisy OneBitGraphicsDisplayImpl so the
// rendered framebuffer is pixel-identical to the device. Throwaway render tool.

#include <cstdint>
#include <cstdlib>

namespace daisy
{
}

// Minimal stand-ins for the daisy text API referenced (but not called on the
// process page) by ui_draw_controls.cpp's DrawRencFocusString6x8.
struct FontDef
{
    uint8_t         FontWidth;
    uint8_t         FontHeight;
    const uint32_t* data;
};
static FontDef Font_6x8 = {6, 8, nullptr};

class OledPager
{
  public:
    static constexpr int kW = 128;
    static constexpr int kH = 64;
    bool buf[kH][kW] = {};

    uint16_t Width() const { return kW; }
    uint16_t Height() const { return kH; }

    void Fill(bool on)
    {
        for(int y = 0; y < kH; ++y)
            for(int x = 0; x < kW; ++x)
                buf[y][x] = on;
    }

    void DrawPixel(uint_fast8_t x, uint_fast8_t y, bool on)
    {
        if(x < kW && y < kH)
            buf[y][x] = on;
    }

    // ---- copied from libDaisy OneBitGraphicsDisplayImpl::DrawLine ----
    void DrawLine(uint_fast8_t x1, uint_fast8_t y1, uint_fast8_t x2, uint_fast8_t y2, bool on)
    {
        int_fast16_t deltaX = std::abs((int_fast16_t)x2 - (int_fast16_t)x1);
        int_fast16_t deltaY = std::abs((int_fast16_t)y2 - (int_fast16_t)y1);
        int_fast16_t signX  = ((x1 < x2) ? 1 : -1);
        int_fast16_t signY  = ((y1 < y2) ? 1 : -1);
        int_fast16_t error  = deltaX - deltaY;
        int_fast16_t error2;

        DrawPixel(x2, y2, on);
        while((x1 != x2) || (y1 != y2))
        {
            DrawPixel(x1, y1, on);
            error2 = error * 2;
            if(error2 > -deltaY)
            {
                error -= deltaY;
                x1 += signX;
            }
            if(error2 < deltaX)
            {
                error += deltaX;
                y1 += signY;
            }
        }
    }

    // ---- copied from libDaisy OneBitGraphicsDisplayImpl::DrawRect ----
    void DrawRect(uint_fast8_t x1, uint_fast8_t y1, uint_fast8_t x2, uint_fast8_t y2, bool on, bool fill = false)
    {
        if(fill)
        {
            for(uint_fast8_t x = x1; x <= x2; x++)
                for(uint_fast8_t y = y1; y <= y2; y++)
                    DrawPixel(x, y, on);
        }
        else
        {
            DrawLine(x1, y1, x2, y1, on);
            DrawLine(x2, y1, x2, y2, on);
            DrawLine(x2, y2, x1, y2, on);
            DrawLine(x1, y2, x1, y1, on);
        }
    }

    // Unused on the process page — present only so ui_draw_controls.cpp links.
    void SetCursor(uint16_t, uint16_t) {}
    char WriteString(const char*, FontDef, bool) { return 0; }
    char WriteChar(char, FontDef, bool) { return 0; }
};
