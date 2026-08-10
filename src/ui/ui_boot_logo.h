#pragma once

#include <stdint.h>

class OledPager;

namespace UiBootLogo
{
static constexpr int kRetroWidth = 128;
static constexpr int kRetroHeight = 64;

// progress: 0.0 = nothing revealed, 1.0 = fully revealed (dithered fade-in).
void DrawRetro(OledPager& display, float progress);
} // namespace UiBootLogo
