#pragma once

#include <cstdint>

class OledPager;

void DrawDottedRect(OledPager& d, int x0, int y0, int x1, int y1, bool on);
void DrawClockwiseMarchingDottedRect(OledPager& d, int x0, int y0, int x1, int y1, uint32_t now_ms);
void DrawCirclePixels(OledPager& d, int cx, int cy, int r, bool on);
void DrawBitmap1bpp(OledPager& disp, int x, int y, int w, int h, int stride, const uint8_t* data, bool on);
