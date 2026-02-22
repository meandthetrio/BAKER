#include "sd_browser_state.h"

#include <cstdio>
#include <cstring>

void SdBrowser_ClearList(SdBrowserState& s)
{
    s.wav_count = 0;
    s.menu_inited = false;
    s.menu.cursor = 0;
    s.menu.scroll = 0;
    s.load_pending = false;
    s.load_pending_index = 0;
    s.save_in_progress = false;
    s.save_progress = 0;
    s.save_status[0] = '\0';
    s.save_name[0] = '\0';
    s.last_loaded_path[0] = '\0';
    for(uint8_t i = 0; i < kSdMaxFiles; ++i)
    {
        s.names[i][0] = '\0';
        s.paths[i][0] = '\0';
        s.items[i].label = "";
        s.items[i].screen = UiScreenId::COUNT;
        s.items[i].req = UiReqType::None;
    }
}

void SdBrowser_SetStatus(SdBrowserState& s, const char* msg)
{
    if(!msg)
    {
        s.status[0] = '\0';
        return;
    }
    std::snprintf(s.status, sizeof(s.status), "%s", msg);
}

void SdBrowser_SetSaveStatus(SdBrowserState& s, const char* msg)
{
    if(!msg)
    {
        s.save_status[0] = '\0';
        return;
    }
    std::snprintf(s.save_status, sizeof(s.save_status), "%s", msg);
}

void SdBrowser_SetSaveName(SdBrowserState& s, const char* name)
{
    if(!name)
    {
        s.save_name[0] = '\0';
        return;
    }
    std::snprintf(s.save_name, sizeof(s.save_name), "%s", name);
}

void SdBrowser_RebuildMenu(SdBrowserState& s)
{
    for(uint8_t i = 0; i < s.wav_count; ++i)
    {
        s.items[i].label = s.names[i];
        s.items[i].screen = UiScreenId::COUNT;
        s.items[i].req = UiReqType::None;
    }
    const uint8_t rows = (s.menu_rows > 0) ? s.menu_rows : 3;
    UiListMenu_Init(s.menu, s.items, s.wav_count, rows);
    s.menu_inited = true;
}
