#pragma once

#include <cstdint>

#include "app_state_project.h"
#include "sd_browser_state.h"
#include "ui_input.h"
#include "ui_list_menu.h"
#include "ui_screens.h"
#include "ui_value_edit.h"

enum class PerformPage : uint8_t
{
    Main = 0,
};

enum class ProjectRenameFocus : uint8_t
{
    Grid = 0,
    Save,
};

enum class ProjectPresetsSortMode : uint8_t
{
    Number = 0,
    Name,
};

static constexpr uint8_t kProjectPresetsHeaderCount = 3;
static constexpr uint8_t kProjectStyleFilterAll = 0xffu;

// Main-thread UI shell, input plumbing, browser/editor navigation, and destructive-menu state.
struct AppUiState
{
    // Shell/navigation state that drives screen routing and redraw decisions.
    PerformPage page = PerformPage::Main;
    bool ui_dirty = true;
    UiNav ui_nav{};
    UiScreenId ui_active_screen = UiScreenId::Hud;
    bool ui_blank_screen_active = false;
    bool ui_lshift_held = false;
    bool ui_rshift_held = false;
    bool ui_trim_preview_hold = false;
    bool ui_trim_preview_gate = false;
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
    uint8_t perform_keyzone_focus = 0; // 0=FULL/SPLIT btn, 1=vel Mod / split pt, 2=mod block A, 3=mod block B
    uint8_t velmod_focus[2] = {2u, 2u}; // 1=threshold 2=send 3=target (default send_amount; matches sim)

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
    bool shift_menu_firmware_update_active = false;
    bool shift_menu_bootloader_armed = false;
    uint32_t shift_menu_bootloader_arm_start_ms = 0;
    bool shift_menu_bootloader_loading = false;
    uint32_t shift_menu_bootloader_loading_start_ms = 0;
    bool sd_delete_mode = false;
    uint16_t sd_delete_index = 0;
    char sd_delete_name[kSdNameMax] = {};
    uint8_t presets_focus_index = kProjectPresetsHeaderCount;
    uint8_t presets_top_row = 0;
    ProjectPresetsSortMode presets_sort_mode = ProjectPresetsSortMode::Number;
    bool presets_sort_descending = false;
    uint8_t presets_style_filter = kProjectStyleFilterAll;
    uint8_t presets_style_picker_cursor = 0;
    uint8_t project_action_cursor = 0;
    uint8_t project_action_style_cursor = 0;
    bool project_style_update_pending = false;
    uint8_t project_style_update_pending_slot = 0;
    uint32_t project_style_update_pending_done_count = 0;
    uint8_t save_project_menu_cursor = 0;
    uint8_t save_project_slot_cursor = 0;
    uint8_t save_project_confirm_cursor = 1;
    bool save_project_pending = false;
    uint8_t save_project_pending_slot = 0;
    uint32_t save_project_pending_done_count = 0;
    bool project_rename_for_new_save = false;
    uint8_t project_rename_new_save_slot = 0;
    bool pending_named_save_active = false;
    uint8_t pending_named_save_slot = 0;
    char pending_named_save_name[kProjectNameMax] = {};
    uint8_t project_rename_grid_col = 0;
    uint8_t project_rename_grid_row = 0;
    uint8_t project_rename_length = 0;
    ProjectRenameFocus project_rename_focus = ProjectRenameFocus::Grid;
    char project_rename_draft[kProjectNameMax] = {};

    UiListMenu sample_edit_menu{};
    bool sample_edit_menu_inited = false;

    // Browser/load/save UI state. Worker code updates this on the main thread,
    // but it is still user-visible UI state rather than audio-visible handoff state.
    SdBrowserState sd{};
};
