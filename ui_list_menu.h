#pragma once

#include <cstdint>

#include "ui_screens.h"
#include "ui_requests.h"

class OledPager;

struct UiMenuItem
{
    const char* label;
    UiScreenId  screen;
    UiReqType   req;
};

struct UiListMenu
{
    const UiMenuItem* items = nullptr;
    uint8_t count = 0;
    uint8_t cursor = 0;
    uint8_t scroll = 0;
    uint8_t rows = 0;
};

void UiListMenu_Init(UiListMenu& menu, const UiMenuItem* items, uint8_t count, uint8_t rows);
bool UiListMenu_OnEnc(UiListMenu& menu, int delta);
void UiListMenu_Render(const UiListMenu& menu, OledPager& oled, int x, int y, int line_h);
