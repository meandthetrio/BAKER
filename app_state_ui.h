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
    // Per-slot, per-plugin param values: craft_param[slot][plugin][param].
    // Each plugin remembers its own 6 raw uint8 params per slot. The meaning of
    // each value is described by the CRAFT descriptor table (src/dsp/craft/
    // craft_params.{h,cpp}); the effect processor decodes it. Switching the
    // plugin on a slot leaves the other plugins' values untouched.
    uint8_t craft_param[3][14][6] = {}; // [slot][plugin][param]; plugin dim == craft::kCraftPluginCount
    // Render action: false = save as new auto-incremented file, true = overwrite
    // the loaded source .wav. Toggled on the Render focus item.
    bool craft_render_overwrite = false;
    char craft_loaded_name[kSdNameMax] = {};
    char craft_loaded_path[kSdPathMax] = {};
    // CRAFT preview model: the in-RAM preview is render-then-play, not live.
    // craft_preview_dirty = an audio-affecting edit (param/plugin/slot-plugin)
    // has happened since the last render, so the preview no longer matches the
    // current config. Drives LED2 (green = matches, orange = needs re-render).
    // Cleared on load (to match the just-loaded raw source) and after a render.
    bool craft_preview_dirty = false;
    // Timestamp (ms) of the most recent audio-affecting CRAFT edit. The worker
    // debounces the auto re-render off this: it only re-renders once the encoder
    // has been still for a short window, so sweeping a param doesn't fire a render
    // per detent (which would stall the encoder). Set on each edit; see
    // MaybeAutoReRenderCraftPreview.
    uint32_t craft_preview_dirty_ms = 0;
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
    // Bake range selector. Width index picks one of four octave spans
    // (0=C4-C5, 1=C3-C6, 2=C2-C7, 3=C1-C8), position offset shifts both
    // bounds by that many octaves (RShift+REnc). Display value is
    // formatted on the fly from these two values; the bake driver uses
    // them to set hdr.lo_note/hi_note for the .bk file written to SD.
    uint8_t bake_range_width_idx       = 3; // default widest span C1-C8
    int8_t  bake_range_position_offset = 0;
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
    // Bake rename: after a successful bake the temp file (/_bake.tmp) is
    // sitting on SD waiting to be named. bake_rename_active routes the
    // RenameProject screen into bake mode (7-char stem cap, ".bk" suffix
    // shown in the display). bake_save_stem holds the chosen stem so the
    // save path can rename /_bake.tmp → /<stem>.bk.
    bool    bake_rename_active = false;
    char    bake_save_stem[8]  = {};  // 7-char stem + NUL
    char    bake_save_status[16] = {}; // "NAME EXISTS" etc.
    // After a successful bake save (/_bake.tmp renamed to /<stem>.bk) the bake
    // screen confirms with "created bake file:" + the stem (bake_save_stem) until
    // the next interaction, and main.cpp double-flashes the LEDs green.
    // bake_created_flash_pending is a one-shot consumed by the main-loop LED code.
    bool    bake_created_show          = false;
    bool    bake_created_flash_pending = false;
    // Transient "saved" confirmation banner shown until
    // preset_saved_overlay_until_ms with the text in saved_overlay_text, plus a
    // one-shot green LED flash (preset_saved_flash_pending). Used by preset save
    // ("PRESET SAVED") and bake save ("BAKE FILE SAVED"). saved_overlay_pending is
    // a one-shot for callers without a timestamp (the bake save path): the UI tick
    // converts it into preset_saved_overlay_until_ms using now_ms.
    uint32_t preset_saved_overlay_until_ms = 0;
    bool     preset_saved_flash_pending    = false;
    char     saved_overlay_text[20]        = {};
    bool     saved_overlay_pending         = false;
    // Engine layer A/B load reuses the SD Manager screen for sortable
    // browsing. engine_load_browser_open routes REnc Click directly to
    // the load action (skipping the action overlay) and changes the
    // bottom-row label to "load a" / "load b". The saved_* fields hold
    // the user's prior SD Manager sort/filter so engine-load can start
    // fresh on each entry without disturbing the main SD Manager state.
    bool    engine_load_browser_open = false;
    bool    engine_load_target_is_b  = false;
    ProjectPresetsSortMode engine_load_saved_sort_mode = ProjectPresetsSortMode::Number;
    bool    engine_load_saved_sort_descending = false;
    uint8_t engine_load_saved_style_filter = kSampleStyleFilterAll;
    uint8_t engine_load_saved_focus_index = kProjectPresetsHeaderCount;
    uint8_t engine_load_saved_top_row = 0;
    uint8_t engine_load_saved_current_index = 0;
    bool    engine_load_state_saved = false;
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
    uint8_t perform_keyzone_focus = 0; // 0 = keytrack, 1 = lane A, 2 = lane B
    // Keyzone keybed play-flash: the last struck note blinks on the keyboard
    // icon until keyzone_flash_until_ms (set by the UI tick on each note-on).
    uint8_t  keyzone_flash_note     = 0;
    uint32_t keyzone_flash_until_ms = 0;
    uint8_t velmod_focus[2] = {2u, 2u}; // 1=threshold 2=amount 3=target 4=shape (default on amount)
    // PerformKeytrack subscreen focus: 0 = vol label, 1 = amount (dB), 2 = tilt
    // rectangle. Each shows a solid border when focused; REnc edits the focused
    // item. Reset to the rectangle on screen enter.
    uint8_t perform_keytrack_focus = 2;

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
    // Auto-normalize loaded samples to -12 dBFS (peak, windowed). Default on.
    // Mirrored into shared.settings_normalize_enabled for the audio thread.
    bool settings_normalize_enabled = true;
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
