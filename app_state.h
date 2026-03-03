#pragma once
#include <atomic>
#include <cstdint>

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
    bool     ui_btn1_held  = false;
    bool     ui_btn2_held  = false;
    UiValueEdit value_edit{};
    uint8_t main_menu_index = 0;
    uint8_t perform_menu_index = 0;
    uint8_t perform_layer = 0; // 0=A, 1=B
    // PERFORM submenu cursor rows (each submenu tracks its own cursor)
    uint8_t perform_engine_row    = 0; // 0=WAVE, 1=LOAD, 2=TUNE
    uint8_t perform_wave_edit_cursor = 0; // 0=TRIM START, 1=TRIM END
    uint8_t perform_adsr_row      = 0; // 0=MODE
    uint8_t perform_emphasis_row  = 0; // 0=GAIN
    int8_t  engine_tune_semitones[2] = {0, 0};
    int8_t  engine_gain_db[2] = {0, 0};
    uint8_t engine_play_mode[2] = {0, 0}; // 0=OneShot, 1=Loop
    char    engine_sample_path[2][kSdPathMax] = {};
    char    engine_sample_name[2][kSdNameMax] = {};
    uint8_t engine_load_target_layer = 0xFFu;
    bool    engine_load_from_perform = false;
    uint32_t engine_seen_applied_gen = 0;
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
