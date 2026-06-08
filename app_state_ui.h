#pragma once

#include <cstdint>

#include "app_state_project.h"
#include "sample_edit.h"
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
    Cancel,
};

enum class ProjectPresetsSortMode : uint8_t
{
    Number = 0,
    Name,
};

enum class RecordRenderPhase : uint8_t
{
    Idle = 0,
    CaptureStarting,
    Capturing,
    Review,
    SaveWait,
};

enum class WaveEditSource : uint8_t
{
    PerformSlot = 0,
    RenderReview,
    SdManage,
};

static constexpr uint8_t kProjectPresetsHeaderCount = 3;
static constexpr uint8_t kProjectStyleFilterAll = 0xffu;
static constexpr uint8_t kSampleStyleFilterAll = 0xffu;
static constexpr uint8_t kRenameDraftMax
    = (kProjectNameMax > (kSdRenameStemMax + 1u)) ? kProjectNameMax
                                                   : static_cast<uint8_t>(kSdRenameStemMax + 1u);

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
    WaveEditSource wave_edit_source = WaveEditSource::PerformSlot;
    bool ui_trim_preview_hold = false;
    bool ui_trim_preview_gate = false;
    // Engine Trim screen gates incoming MIDI note-on/off so the window can be
    // auditioned in isolation. Written by UiTick, read by the MIDI handlers;
    // both run on the main loop thread, so a plain bool is sufficient.
    // The gate is now driven by the union of (a) engine-trim active and
    // (b) the samples menu being anywhere on the nav stack — so MIDI is
    // silenced across record/craft/bake/SD-manager too. The engine-trim
    // falling-edge cleanup (win_preview_stop_req) still fires only when the
    // engine-trim screen specifically deactivates; ui_engine_trim_was_active
    // is the tracked previous state used to detect that edge independently
    // of the gate flag.
    bool ui_midi_gate_active = false;
    bool ui_engine_trim_was_active = false;
    // ADSR seam-edit screen: MIDI is forced monophonic and restricted to the
    // edited layer for live seam auditioning. Plain bools (main-loop thread only).
    bool ui_seam_audition_active = false;
    uint8_t ui_seam_audition_layer = 0;
    bool ui_seam_silence_pending = false;
    // Seam-edit entry snapshot, restored on discard (Pod-encoder click).
    float ui_seam_entry_amount = 0.0625f;
    float ui_seam_entry_shape = 0.0f;
    bool ui_seam_entry_baked = false;
    // Pending swap of the playback buffer for the seam-edit screen.
    //   0 = none, 1 = swap to raw (enter edit, live crossfade), 2 = re-bake (exit).
    // The swap waits for ui_seam_bake_delay_ticks UI ticks after silence_pending
    // is set, so AllNotesOff propagates and active voices fade before we write
    // to the playback buffer.
    uint8_t ui_seam_bake_pending = 0;
    uint8_t ui_seam_bake_layer = 0;
    uint8_t ui_seam_bake_delay_ticks = 0;
    float   ui_seam_bake_amount = 0.0625f;
    float   ui_seam_bake_shape = 0.0f;
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
    // Controls which file types the SD scan worker exposes in the SdBrowser
    // list. Set by the screen that pushes SdBrowse before kicking the scan,
    // honored by ui_worker_sd_scan when adding entries. Layer B's engine
    // load is the only consumer of WavAndBk in the v1 of .bk playback;
    // everywhere else (layer A load, bake-source picker) keeps WavOnly.
    enum class SdScanFilter : uint8_t
    {
        WavOnly = 0,
        WavAndBk,
    };
    SdScanFilter sd_scan_filter = SdScanFilter::WavOnly;
    uint8_t main_menu_index = 0;
    uint8_t samples_menu_index = 0;
    uint8_t record_menu_index = 0;
    uint8_t craft_focus = 0;
    uint8_t craft_active_slot = 0;
    bool craft_browser_open = false;
    bool craft_browser_wait_for_load = false;
    uint8_t craft_slot_plugin[3] = {0, 0, 0};
    uint8_t craft_capture_rate[3] = {0, 0, 0};
    uint8_t craft_capture_bits[3] = {0, 0, 0};
    uint8_t craft_capture_input[3] = {0, 0, 0};
    uint8_t craft_capture_filter[3] = {0, 0, 0};
    uint8_t craft_capture_curve[3] = {0, 0, 0};
    uint8_t craft_capture_age[3] = {0, 0, 0};
    char craft_loaded_name[kSdNameMax] = {};
    char craft_loaded_path[kSdPathMax] = {};
    // Bake screen (Layer B baker entry point). bake_focus selects which of the
    // three rows is focused (0=sample, 1=root, 2=bake). bake_root_note is the
    // MIDI note assigned to the source sample's native pitch (default C4=60).
    // bake_sample_path/_name hold the user-selected source .wav. Empty path
    // means "no sample picked" and the UI shows literal placeholder "sample".
    // bake_browser_open routes SD-browser selections back to the bake screen
    // (bypassing the engine-load path), mirroring craft_browser_open.
    uint8_t bake_focus = 0;
    uint8_t bake_root_note = 60;
    char    bake_sample_path[kSdPathMax] = {};
    char    bake_sample_name[kSdNameMax] = {};
    bool    bake_browser_open = false;
    // STAGE 1 TEMPORARY: shows last silence-bake test result on the bake
    // screen (so we can verify the writer ran and what it returned without
    // needing SD inspection). Empty = no test run yet. Removed in stage 3
    // when the real progress screen lands.
    char    bake_test_status[20] = {};
    // STAGE 2 TEMPORARY: bake-in-progress overlay. When `bake_progress_active`
    // is true the bake screen draws a centered modal showing "X/2" (slices
    // completed of total), the current PSOLA phase label, and a 0-100 %
    // progress bar. The bake worker updates these between phases and calls
    // Pod_ForceDisplayRefresh() to push the new text to the OLED. Stage 3
    // promotes this to a dedicated progress screen with cancel chord.
    bool    bake_progress_active     = false;
    uint8_t bake_progress_slice_done  = 0;  // X in "X/N" — completed PSOLA slices
    uint8_t bake_progress_slice_total = 0;  // N = total PSOLA jobs this bake (≤47);
                                            // set per-run by the bake driver.
    uint8_t bake_progress_percent    = 0;   // 0..100
    char    bake_progress_label[20]  = {};  // e.g. "psola: pitch shift"
    bool record_menu_source_override_active = false;
    uint8_t record_menu_source_override = 0;
    bool record_menu_armed_back_returns_to_menu = false;
    uint8_t record_render_focus = 0;
    int8_t record_render_note_offset = 0;
    uint16_t record_render_hold_ms = 250;
    bool record_render_preview_trigger_pending = false;
    bool record_render_preview_note_active = false;
    uint8_t record_render_preview_note = 60;
    RecordRenderPhase record_render_phase = RecordRenderPhase::Idle;
    uint8_t record_render_note = 60;
    uint32_t record_render_capture_started_ms = 0;
    bool record_render_all_notes_off_sent = false;
    uint32_t record_render_note_on_due_ms = 0;
    uint32_t record_render_note_off_due_ms = 0;
    bool record_render_note_on_sent = false;
    bool record_render_note_off_sent = false;
    uint8_t record_render_review_focus = 0;
    char record_render_status[kSdStatusMax] = {};
    SampleEdit render_review_trim_entry{};
    bool render_review_trim_has_entry = false;
    SampleEdit sd_manage_trim_entry{};
    bool sd_manage_trim_has_entry = false;
    bool render_sample_rename_active = false;
    bool render_sample_rename_wait_for_worker = false;
    char record_render_save_stem[kSdRenameStemMax + 1u] = {};
    bool sd_manage_edit_active = false;
    bool sd_manage_edit_wait_for_load = false;
    bool sd_manage_trim_rename_active = false;
    bool sd_manage_trim_wait_for_worker = false;
    bool sd_manage_trim_save_busy = false;
    uint16_t sd_manage_edit_index = 0;
    uint8_t sd_manage_trim_choice_cursor = 0;
    char sd_manage_save_stem[kSdRenameStemMax + 1u] = {};
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
    bool settings_mic_monitor_enabled = false;
    bool shift_menu_firmware_update_active = false;
    bool shift_menu_bootloader_armed = false;
    uint32_t shift_menu_bootloader_arm_start_ms = 0;
    bool shift_menu_bootloader_loading = false;
    uint32_t shift_menu_bootloader_loading_start_ms = 0;
    bool sd_delete_mode = false;
    uint16_t sd_delete_index = 0;
    char sd_delete_name[kSdNameMax] = {};
    uint8_t sd_delete_confirm_cursor = 1u;
    bool sample_rename_active = false;
    uint16_t sample_rename_index = 0;
    uint8_t sd_manage_focus_index = kProjectPresetsHeaderCount;
    uint8_t sd_manage_top_row = 0;
    ProjectPresetsSortMode sd_manage_sort_mode = ProjectPresetsSortMode::Number;
    bool sd_manage_sort_descending = false;
    uint8_t sd_manage_style_filter = kSampleStyleFilterAll;
    uint8_t sd_manage_style_picker_cursor = 0;
    uint8_t sd_manage_visible_order[kSdMaxFiles] = {};
    uint8_t sd_manage_visible_count = 0;
    uint8_t sd_manage_current_index = 0;
    uint8_t sd_manage_action_cursor = 0;
    uint8_t sd_manage_style_cursor = 0;
    uint8_t presets_focus_index = kProjectPresetsHeaderCount;
    uint8_t presets_top_row = 0;
    ProjectPresetsSortMode presets_sort_mode = ProjectPresetsSortMode::Number;
    bool presets_sort_descending = false;
    uint8_t presets_style_filter = kProjectStyleFilterAll;
    uint8_t presets_style_picker_cursor = 0;
    uint8_t project_action_cursor = 0;
    uint8_t project_action_style_cursor = 0;
    bool project_delete_mode = false;
    uint8_t project_delete_slot = 0;
    uint8_t project_delete_confirm_cursor = 1u;
    uint32_t project_status_loaded_since_ms = 0;
    bool project_style_update_pending = false;
    uint8_t project_style_update_pending_slot = 0;
    uint32_t project_style_update_pending_done_count = 0;
    bool project_delete_pending = false;
    uint8_t project_delete_pending_slot = 0;
    uint32_t project_delete_pending_done_count = 0;
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
    char project_rename_draft[kRenameDraftMax] = {};

    UiListMenu sample_edit_menu{};
    bool sample_edit_menu_inited = false;

    // Browser/load/save UI state. Worker code updates this on the main thread,
    // but it is still user-visible UI state rather than audio-visible handoff state.
    SdBrowserState sd{};
};
