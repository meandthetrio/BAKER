#pragma once

#include <cstdint>

#include "sd_browser_state.h"
#include "ui_input.h"
#include "ui_list_menu.h"
#include "ui_screens.h"
#include "ui_value_edit.h"

enum class PerformPage : uint8_t
{
    Main = 0,
};

// Main-thread UI shell, input plumbing, browser/editor navigation, and destructive-menu state.
struct AppUiState
{
    // Shell/navigation state that drives screen routing and redraw decisions.
    PerformPage page = PerformPage::Main;
    bool ui_dirty = true;
    UiNav ui_nav{};
    UiScreenId ui_active_screen = UiScreenId::Hud;
    bool ui_lshift_held = false;
    bool ui_rshift_held = false;
    bool ui_parent_preview_active = false;
    uint8_t ui_parent_preview_from_top = 0;
    uint8_t ui_parent_preview_mode = 0;
    UiScreenId ui_parent_preview_origin_screen = UiScreenId::COUNT;
    uint8_t ui_parent_preview_origin_main_cursor = 0;
    uint8_t ui_parent_preview_origin_fx_cursor = 0;
    bool ui_parent_preview_origin_process_detail = false;
    bool ui_parent_preview_origin_process_eq_graph = false;
    bool ui_btn1_held = false;
    bool ui_btn2_held = false;
    uint8_t main_menu_index = 0;

    // Input queues and helper widgets used entirely on the main/UI thread.
    uint32_t last_input_ms = 0;
    uint32_t ui_hz = 0;
    uint32_t ctrl_hz = 0;
    UiInputQueue ui_in{};
    uint32_t ui_in_push = 0;
    uint32_t ui_in_pop = 0;
    uint32_t ui_in_ovf = 0;
    uint32_t ui_in_hi = 0;
    UiListMenu hud_menu{};
    bool hud_menu_inited = false;
    UiValueEdit value_edit{};

    // SHIFT menu and destructive browser flow state.
    uint8_t shift_menu_cursor = 0;
    bool shift_menu_edit_volume = false;
    bool sd_delete_mode = false;
    uint16_t sd_delete_index = 0;
    char sd_delete_name[kSdNameMax] = {};

    UiListMenu sample_edit_menu{};
    bool sample_edit_menu_inited = false;

    // Browser/load/save UI state. Worker code updates this on the main thread,
    // but it is still user-visible UI state rather than audio-visible handoff state.
    SdBrowserState sd{};
};
