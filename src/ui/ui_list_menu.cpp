#include "ui_list_menu.h"

#include "oled_pager.h"
#include "ui_draw_controls.h"

#include <cstdio>

using namespace daisy;

void UiListMenu_Init(UiListMenu& menu, const UiMenuItem* items, uint8_t count, uint8_t rows)
{
    menu.items = items;
    menu.count = count;
    menu.cursor = 0;
    menu.scroll = 0;
    menu.rows = rows;
}

bool UiListMenu_OnEnc(UiListMenu& menu, int delta)
{
    if(menu.count == 0 || delta == 0)
        return false;

    int new_cursor = static_cast<int>(menu.cursor) + delta;
    const int count = static_cast<int>(menu.count);
    while(new_cursor < 0)
        new_cursor += count;
    while(new_cursor >= count)
        new_cursor -= count;

    uint8_t new_scroll = menu.scroll;
    if(menu.rows > 0)
    {
        if(new_cursor < new_scroll)
            new_scroll = static_cast<uint8_t>(new_cursor);
        else if(new_cursor >= static_cast<int>(new_scroll + menu.rows))
            new_scroll = static_cast<uint8_t>(new_cursor - (menu.rows - 1));
    }

    const bool changed = (menu.cursor != static_cast<uint8_t>(new_cursor))
                         || (menu.scroll != new_scroll);
    menu.cursor = static_cast<uint8_t>(new_cursor);
    menu.scroll = new_scroll;
    return changed;
}

void UiListMenu_Render(const UiListMenu& menu, OledPager& oled, int x, int y, int line_h)
{
    if(!menu.items || menu.count == 0 || menu.rows == 0)
        return;

    char buf[20];
    for(uint8_t row = 0; row < menu.rows; ++row)
    {
        const uint8_t idx = static_cast<uint8_t>(menu.scroll + row);
        if(idx >= menu.count)
            break;

        const int row_y = y + static_cast<int>(row) * line_h;
        std::snprintf(buf, sizeof(buf), "%s", menu.items[idx].label);
        if(idx == menu.cursor)
        {
            DrawRencFocusString6x8(oled, buf, x + 6, row_y);
        }
        else
        {
            oled.SetCursor(x + 6, row_y);
            oled.WriteString(buf, Font_6x8, true);
        }
    }
}
