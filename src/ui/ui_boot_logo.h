#pragma once

#include <stdint.h>

class OledPager;

namespace UiBootLogo
{
static constexpr int kLogoWidth = 50;
static constexpr int kLogoHeight = 64;
static constexpr int kRetroWidth = 128;
static constexpr int kRetroHeight = 64;

void Draw(OledPager& display);
void DrawRetro(OledPager& display);
} // namespace UiBootLogo
