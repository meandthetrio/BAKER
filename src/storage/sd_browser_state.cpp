#include "sd_browser_state.h"

#include "app_state_shared.h"

#include <cstdio>
#include <cstring>

namespace
{
bool CopyStringChecked(const char* src, char* out, size_t out_n)
{
    if(!out || out_n == 0u)
        return false;
    if(!src)
    {
        out[0] = '\0';
        return false;
    }

    const int written = std::snprintf(out, out_n, "%s", src);
    if(written < 0 || written >= static_cast<int>(out_n))
    {
        out[out_n - 1u] = '\0';
        return false;
    }
    return true;
}
} // namespace

void SdWavLoad_SetBusy(AppSharedState& shared, SdBrowserState& sd, bool busy)
{
    shared.sample.publish.sd_wav_load_busy.store(busy ? 1u : 0u, std::memory_order_release);
    sd.sd_wav_load_busy = busy;
}

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
        s.display_names[i][0] = '\0';
        s.paths[i][0] = '\0';
        s.styles[i] = SampleStyle::None;
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
        s.items[i].label = s.display_names[i];
        s.items[i].screen = UiScreenId::COUNT;
        s.items[i].req = UiReqType::None;
    }
    const uint8_t rows = (s.menu_rows > 0) ? s.menu_rows : 3;
    UiListMenu_Init(s.menu, s.items, s.wav_count, rows);
    s.menu_inited = true;
}

bool SdBrowser_SetWavFileAtIndex(SdBrowserState& s, uint16_t idx, const char* name, const char* path)
{
    if(idx >= kSdMaxFiles || !name || !path || name[0] == '\0' || path[0] == '\0')
        return false;

    std::snprintf(s.names[idx],
                  sizeof(s.names[idx]),
                  "%.*s",
                  static_cast<int>(sizeof(s.names[idx]) - 1u),
                  name);
    if(!CopyStringChecked(path, s.paths[idx], sizeof(s.paths[idx])))
        return false;

    char full_display[kSdPathMax];
    if(BuildSampleDisplayName(path, full_display, sizeof(full_display)))
    {
        std::snprintf(s.display_names[idx],
                      sizeof(s.display_names[idx]),
                      "%.*s",
                      static_cast<int>(sizeof(s.display_names[idx]) - 1u),
                      full_display);
    }
    else if(!BuildSampleDisplayName(s.names[idx], s.display_names[idx], sizeof(s.display_names[idx])))
    {
        char fallback_name[kSdNameMax];
        std::snprintf(fallback_name,
                      sizeof(fallback_name),
                      "%.*s",
                      static_cast<int>(sizeof(fallback_name) - 1u),
                      s.names[idx]);
        std::snprintf(s.display_names[idx],
                      sizeof(s.display_names[idx]),
                      "%.*s",
                      static_cast<int>(sizeof(s.display_names[idx]) - 1u),
                      fallback_name);
    }

    s.styles[idx] = ParseSampleStyleFromFilename(path);
    return true;
}

bool SdBrowser_AddWavFile(SdBrowserState& s, const char* name, const char* path)
{
    if(!name || !path || name[0] == '\0' || path[0] == '\0')
        return false;

    const bool had_full_scan = s.scan_done;

    for(uint8_t i = 0; i < s.wav_count; ++i)
    {
        if(std::strcmp(s.paths[i], path) == 0)
        {
            if(!SdBrowser_SetWavFileAtIndex(s, i, name, path))
                return false;
            SdBrowser_RebuildMenu(s);
            s.scan_done = had_full_scan;
            return true;
        }
    }

    if(s.wav_count >= kSdMaxFiles)
        return false;

    const uint8_t old_cursor = s.menu.cursor;
    const uint8_t old_scroll = s.menu.scroll;
    const uint8_t idx = s.wav_count++;

    if(!SdBrowser_SetWavFileAtIndex(s, idx, name, path))
    {
        s.wav_count--;
        return false;
    }

    SdBrowser_RebuildMenu(s);
    if(idx > 0u)
    {
        s.menu.cursor = old_cursor;
        s.menu.scroll = old_scroll;
    }
    s.scan_done = had_full_scan;
    return true;
}

void SdBrowser_RemoveWavAtIndex(SdBrowserState& s, uint16_t idx)
{
    if(idx >= s.wav_count)
        return;

    for(uint8_t i = static_cast<uint8_t>(idx); i + 1u < s.wav_count; ++i)
    {
        std::memcpy(s.names[i], s.names[i + 1u], sizeof(s.names[i]));
        std::memcpy(s.display_names[i], s.display_names[i + 1u], sizeof(s.display_names[i]));
        std::memcpy(s.paths[i], s.paths[i + 1u], sizeof(s.paths[i]));
        s.styles[i] = s.styles[i + 1u];
    }

    s.wav_count--;
    s.names[s.wav_count][0] = '\0';
    s.display_names[s.wav_count][0] = '\0';
    s.paths[s.wav_count][0] = '\0';
    s.styles[s.wav_count] = SampleStyle::None;
    s.items[s.wav_count].label = "";
    s.items[s.wav_count].screen = UiScreenId::COUNT;
    s.items[s.wav_count].req = UiReqType::None;

    const uint8_t rows = s.menu_rows;
    SdBrowser_RebuildMenu(s);
    if(s.wav_count == 0u)
        return;

    uint8_t cursor = static_cast<uint8_t>(idx);
    if(cursor >= s.wav_count)
        cursor = static_cast<uint8_t>(s.wav_count - 1u);

    s.menu.cursor = cursor;
    if(rows > 0u)
    {
        if(s.menu.scroll > cursor)
            s.menu.scroll = cursor;
        else if(cursor >= static_cast<uint8_t>(s.menu.scroll + rows))
            s.menu.scroll = static_cast<uint8_t>(cursor - (rows - 1u));
    }
}
