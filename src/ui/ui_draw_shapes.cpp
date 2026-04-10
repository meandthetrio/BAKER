#include "ui_draw_shapes.h"

#include "oled_pager.h"

void DrawDottedRect(OledPager& d, int x0, int y0, int x1, int y1, bool on)
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

void DrawClockwiseMarchingDottedRect(OledPager& d, int x0, int y0, int x1, int y1, uint32_t now_ms)
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

void DrawCirclePixels(OledPager& d, int cx, int cy, int r, bool on)
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

void DrawBitmap1bpp(OledPager& disp,
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
