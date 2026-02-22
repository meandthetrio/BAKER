#pragma once

#include <cstdint>

struct AppState;
struct UiLayout;
class OledPager;

struct UiOverlayState
{
    bool     visible = false;
    uint32_t shown_since_ms = 0;
};

void UiOverlay_Update(UiOverlayState& o, uint32_t now_ms, bool shift_held, bool editing);
void UiOverlay_Render(const AppState& app, const UiLayout& layout, OledPager& oled);
