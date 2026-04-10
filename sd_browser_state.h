#pragma once

#include <cstdint>

#include "ui_list_menu.h"

static constexpr uint8_t kSdMaxFiles = 32;
static constexpr uint8_t kSdNameMax = 24;
static constexpr uint8_t kSdPathMax = 64;
static constexpr uint8_t kSdStatusMax = 16;

struct SdBrowserState
{
    bool sd_inited = false;
    bool sd_ok = false;
    bool scan_in_progress = false;
    bool scan_done = false;
    uint8_t wav_count = 0;
    char names[kSdMaxFiles][kSdNameMax] = {};
    char paths[kSdMaxFiles][kSdPathMax] = {};
    UiMenuItem items[kSdMaxFiles] = {};
    UiListMenu menu{};
    bool menu_inited = false;
    uint8_t menu_rows = 0;
    char status[kSdStatusMax] = {};
    uint8_t load_progress = 0;
    bool load_in_progress = false;
    uint16_t last_loaded_index = 0xFFFFu;
    char last_loaded_path[kSdPathMax] = {};
    bool load_pending = false;
    uint16_t load_pending_index = 0;
    bool save_in_progress = false;
    uint8_t save_progress = 0;
    char save_status[kSdStatusMax] = {};
    char save_name[kSdNameMax] = {};
    uint32_t wav_err_count = 0;
};

void SdBrowser_ClearList(SdBrowserState& s);
void SdBrowser_SetStatus(SdBrowserState& s, const char* msg);
void SdBrowser_SetSaveStatus(SdBrowserState& s, const char* msg);
void SdBrowser_SetSaveName(SdBrowserState& s, const char* name);
void SdBrowser_RebuildMenu(SdBrowserState& s);
