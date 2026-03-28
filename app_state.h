#pragma once
#include <atomic>
#include <cstdint>

#include "keygroups.h"
#include "mod_matrix.h"
#include "plocks.h"
#include "macros.h"
#include "ui_input.h"
#include "ui_screens.h"
#include "ui_list_menu.h"
#include "ui_value_edit.h"
#include "ui_overlay.h"
#include "ui_requests.h"
#include "sd_browser_state.h"
#include "sampler_sample.h"
#include "sd_sample_pool.h"
#include "sample_edit.h"

enum class PerformPage : uint8_t
{
    Main = 0,
    // Later: Delay, Reverb, Sat, etc.
};

enum class RecordInputSource : uint8_t
{
    LineIn = 0,
    Mic = 1,
};

enum class RecordUiState : uint8_t
{
    SourceSelect = 0,
    Armed,
    Countdown,
    Recording,
    Review,
    TargetSelect,
    BackConfirm,
    SaveWait,
};

struct AppState
{
    PerformPage page = PerformPage::Main;

    // UI “dirty flag” = something visible changed and we should redraw.
    bool ui_dirty = true;

    // Debug / proof-of-life counters for SPSC event queue.
    std::atomic<uint32_t> events_pushed{0};
    std::atomic<uint32_t> events_popped{0};
    std::atomic<uint32_t> queue_overflows{0};
    std::atomic<uint32_t> midi_rx_count{0};
    std::atomic<uint32_t> loop_mode{0}; // 0=FWD, 1=PINGPONG
    std::atomic<uint32_t> clip_count{0};
    std::atomic<uint32_t> fadeouts_started{0};

    // Voice engine debug (written by audio thread, read by UI).
    std::atomic<uint32_t> voices_active{0};
    std::atomic<uint32_t> voices_peak_1s{0};
    std::atomic<uint32_t> voice_steals{0};
    std::atomic<uint32_t> last_stolen_voice_index{0};
    std::atomic<uint32_t> last_stolen_start_id{0};
    std::atomic<uint32_t> last_new_start_id{0};

    // Audio thread diagnostics.
    std::atomic<uint32_t> audio_cycles_last{0};
    std::atomic<uint32_t> audio_cycles_peak{0};
    std::atomic<uint32_t> audio_budget_cycles{0};
    std::atomic<uint32_t> audio_late_count{0};
    // Packed {voice_idx, note, velocity} in low 24 bits.
    std::atomic<uint32_t> last_voice_packed{0};
    std::atomic<uint32_t> last_sample_index{0};
    std::atomic<uint32_t> last_vel_layer{0};
    std::atomic<uint32_t> last_velocity{0};
    std::atomic<int32_t> last_lfo{0};
    std::atomic<int32_t> last_env{0};
    std::atomic<uint32_t> lfo_rate_dbg{0};
    std::atomic<uint32_t> lfo_depth_dbg{0};
    std::atomic<uint32_t> playhead_frame[2]{{0}, {0}};
    std::atomic<uint32_t> playhead_active[2]{{0}, {0}};

    // Mod matrix (main loop owns edits, audio thread consumes snapshot).
    ModMatrixState mod_matrix{};
    ModRoute       mod_routes_ui[kMaxModRoutes]{};
    uint8_t        mod_route_selected = 0;

    // Parameter locks (main loop owns pattern and clock).
    PLocksState plocks{};
    Pattern     plock_pattern{};
    bool        seq_running = false;
    bool        plock_apply_enabled = false;
    std::atomic<uint8_t> lfo_wave{0}; // 0=SINE, 1=PULSE
    uint32_t    seq_bpm = 120;
    uint32_t    seq_last_ms = 0;
    uint32_t    seq_accum_ms = 0;

    // Performance macros (main loop edits, audio thread latches).
    MacroState macro_ui{};
    MacroState macro_a{};
    MacroState macro_b{};
    std::atomic<uint8_t>  macro_sel{0};
    std::atomic<uint32_t> macro_gen{0};

    // Main-loop owned UI helpers (not accessed from audio thread).
    uint32_t last_input_ms = 0;
    uint32_t ui_hz         = 0;
    uint32_t ctrl_hz       = 0;
    UiInputQueue ui_in{};
    uint32_t ui_in_push    = 0;
    uint32_t ui_in_pop     = 0;
    uint32_t ui_in_ovf     = 0;
    uint32_t ui_in_hi      = 0;
    UiReqQueue ui_req_q{};
    uint32_t ui_req_ovf = 0;
    uint32_t ui_req_push = 0;
    uint32_t ui_req_pop = 0;
    bool     ui_req_busy = false;
    UiReqType ui_req_active = UiReqType::None;
    uint8_t  ui_req_progress = 0;
    int8_t   ui_req_result = 0;
    uint16_t ui_req_arg0 = 0;
    uint32_t ui_req_done_count = 0;
    uint32_t ui_req_work_units_done = 0;
    uint32_t ui_req_work_units_total = 0;
    UiNav    ui_nav{};
    UiScreenId ui_active_screen = UiScreenId::Hud;
    UiListMenu hud_menu{};
    bool     hud_menu_inited = false;
    bool     ui_lshift_held = false;
    bool     ui_rshift_held = false;
    bool     ui_parent_preview_active = false;
    uint8_t  ui_parent_preview_from_top = 0;
    uint8_t  ui_parent_preview_mode = 0; // 0=none, 1=nav parent, 2=process detail parent
    UiScreenId ui_parent_preview_origin_screen = UiScreenId::COUNT;
    uint8_t  ui_parent_preview_origin_main_cursor = 0;
    uint8_t  ui_parent_preview_origin_fx_cursor = 0;
    bool     ui_parent_preview_origin_process_detail = false;
    bool     ui_btn1_held  = false;
    bool     ui_btn2_held  = false;
    UiValueEdit value_edit{};
    uint8_t main_menu_index = 0;
    uint8_t perform_menu_index = 0;
    uint8_t perform_layer = 0; // 0=A, 1=B
    // PERFORM submenu cursor rows (each submenu tracks its own cursor)
    uint8_t perform_engine_row    = 0; // 0=WAVE, 1=LOAD, 2=TUNE
    uint8_t perform_wave_edit_cursor = 0; // 0=TRIM START, 1=TRIM END
    SampleEdit perform_wave_edit_entry[kSdSampleSlots]{};
    bool    perform_wave_edit_has_entry = false;
    uint8_t perform_keyzone_lo_note[2] = {kPerformKeyzoneMinNote, kPerformKeyzoneMinNote};
    uint8_t perform_keyzone_hi_note[2] = {kPerformKeyzoneMaxNote, kPerformKeyzoneMaxNote};
    uint8_t perform_adsr_row[2]   = {1u, 1u}; // 0=1SHOT, 1=LOOP, 2=ADSR
    bool    perform_adsr_type_focus = false;
    uint8_t perform_adsr_stage_focus = 0; // 0=A, 1=D, 2=S, 3=R
    uint8_t perform_adsr_loop_attack[2] = {5u, 5u}; // 1..100
    uint8_t perform_adsr_loop_decay[2] = {20u, 20u}; // 1..100
    uint8_t perform_adsr_loop_sustain[2] = {100u, 100u}; // 0..100
    uint8_t perform_adsr_loop_release[2] = {50u, 50u}; // 1..100
    uint8_t perform_adsr_env_a_x[2] = {13u, 13u}; // 0..100, left->right
    uint8_t perform_adsr_env_d_x[2] = {38u, 38u}; // 0..100, must stay after A
    uint8_t perform_adsr_env_r_x[2] = {89u, 89u}; // 0..100, must stay after D
    uint8_t perform_adsr_env_s_level[2] = {50u, 50u}; // 0..100, low->high
    uint8_t perform_emphasis_row  = 0; // 0=GAIN, 1=FILTER, 2=RESO
    uint8_t perform_process_fx_cursor = 0; // 0=S, 1=M, 2=D, 3=R
    uint8_t perform_process_fx_order[4] = {0, 1, 2, 3}; // 0=S,1=M,2=D,3=R
    uint8_t perform_process_main_cursor = 2; // 0=VOL A, 1=VOL B, 2..5=S/M/D/R
    uint16_t perform_process_vol_pct[2] = {100u, 100u}; // 0..200, UNITY=100
    bool    perform_process_vol_muted[2] = {false, false};
    float   perform_process_vol_unmuted_level[2] = {1.0f, 1.0f};
    bool    perform_process_detail_active = false;
    uint8_t perform_process_detail_param[4] = {0, 0, 0, 0};
    RecordUiState record_state = RecordUiState::SourceSelect;
    uint8_t record_source_index = 0; // 0=LINE IN, 1=MIC
    uint8_t record_target_index = 0; // 0=SAVE, 1=RECORD AGAIN
    uint32_t record_countdown_start_ms = 0;
    double record_anim_start_ms = -1.0;
    uint8_t record_slot = 0;
    bool    record_preview_hold = false;
    bool    record_preview_gate = false;
    int8_t  engine_tune_semitones[2] = {0, 0};
    int8_t  engine_gain_db[2] = {0, 0};
    uint8_t engine_play_mode[2] = {0, 0}; // 0=OneShot, 1=Loop
    char    engine_sample_path[2][kSdPathMax] = {};
    char    engine_sample_name[2][kSdNameMax] = {};
    uint8_t engine_load_target_layer = 0xFFu;
    bool    engine_load_from_perform = false;
    uint32_t engine_seen_applied_gen = 0;
    uint32_t engine_header_invert_until_ms = 0;
    uint8_t fx_field_cursor = 0;
    uint8_t mod_field_cursor = 0;

    // SHIFT menu (opened by POD BUTTON1)
    uint8_t shift_menu_cursor = 0; // 0=DELETE, 1=VOLUME
    bool    shift_menu_edit_volume = false;

    // SD delete flow (entered via SHIFT→DELETE)
    bool     sd_delete_mode = false;      // when true, EXT ENC selects file for deletion
    uint16_t sd_delete_index = 0;
    char     sd_delete_name[kSdNameMax] = {};

    UiOverlayState overlay{};
    uint16_t render_ms = 0;
    uint16_t render_hi_ms = 0;
    uint32_t render_skips = 0;
    uint32_t render_frames = 0;
    uint32_t render_cooldown_until_ms = 0;
    SdBrowserState sd{};
    Sample sd_slots[kSdSampleSlots]{};
    std::atomic<uint8_t> sd_current_slot{0};
    std::atomic<uint8_t> sd_published_slot{0};
    std::atomic<uint8_t> sd_published_ready{0};
    std::atomic<uint32_t> sd_published_gen{0};
    std::atomic<uint32_t> sd_applied_gen{0};
    SampleEdit sd_edit_slots[kSdSampleSlots]{};
    SampleEdit sd_edit_pending{};
    std::atomic<uint8_t> sd_edit_slot{0};
    std::atomic<uint8_t> sd_edit_ready{0};
    std::atomic<uint32_t> sd_edit_gen{0};
    std::atomic<uint32_t> sd_edit_applied_gen{0};

    // Recording (UI thread commands, audio thread capture).
    std::atomic<uint8_t> rec_source_sel{0}; // 0=LINE IN, 1=MIC
    std::atomic<uint8_t> rec_monitor_enable{0};
    std::atomic<uint8_t> rec_start_req{0};
    std::atomic<uint8_t> rec_stop_req{0};
    std::atomic<uint8_t> rec_active{0};
    std::atomic<uint8_t> rec_slot_pending{0};
    std::atomic<uint32_t> rec_pos{0};
    std::atomic<uint32_t> rec_length{0};
    std::atomic<uint32_t> rec_live_gen{0};
    int16_t rec_live_min[128] = {};
    int16_t rec_live_max[128] = {};
    int16_t rec_live_last_col = -1;

    UiListMenu sample_edit_menu{};
    bool sample_edit_menu_inited = false;
    char project_status[16] = {};
    bool project_edit_pending = false;
    SampleEdit project_pending_edit{};
};

static inline const char* WaveChar(uint8_t w)
{
    return (w == 0) ? "S" : "P";
}
