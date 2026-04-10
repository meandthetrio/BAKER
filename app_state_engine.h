#pragma once

#include <cstdint>

#include "keygroups.h"
#include "sample_edit.h"
#include "storage_limits.h"

// App-owned engine/editor state and perform editing state.
struct AppEngineState
{
    // Per-layer sample/editor metadata surfaced to the UI.
    int8_t  engine_tune_semitones[2] = {0, 0};
    int16_t engine_gain_db[2] = {0, 0};
    uint8_t engine_drive_mode[2] = {0u, 0u};
    uint8_t engine_play_mode[2] = {1, 1};
    char    engine_sample_path[2][kSdPathMax] = {};
    char    engine_sample_name[2][kSdNameMax] = {};
    uint8_t engine_load_target_layer = 0xFFu;
    bool    engine_load_from_perform = false;
    uint32_t engine_seen_applied_gen = 0;
    uint32_t engine_header_invert_until_ms = 0;

    // PERFORM screen editor state and submenu-local cursors.
    uint8_t perform_menu_index = 0;
    uint8_t perform_layer = 0;
    uint8_t perform_engine_row = 0;
    uint8_t perform_wave_edit_cursor = 0;
    SampleEdit perform_wave_edit_entry[kSdSampleSlots]{};
    bool    perform_wave_edit_has_entry = false;
    uint8_t perform_keyzone_lo_note[2] = {48u, 48u};
    uint8_t perform_keyzone_hi_note[2] = {60u, 60u};
    uint8_t perform_keyzone_marker_focus = 0;
    uint8_t perform_keyzone_window_octave[2] = {2u, 2u};
    uint8_t perform_adsr_row[2] = {1u, 1u};
    bool    perform_adsr_type_focus = false;
    bool    perform_adsr_wave_focus = false;
    uint8_t perform_adsr_stage_focus = 0;
    uint16_t perform_adsr_loop_attack[2] = {5u, 5u};
    uint8_t perform_adsr_loop_decay[2] = {20u, 20u};
    uint8_t perform_adsr_loop_sustain[2] = {100u, 100u};
    uint16_t perform_adsr_loop_release[2] = {50u, 50u};
    float   perform_adsr_loop_crossfade[2] = {0.0625f, 0.0625f};
    float   perform_adsr_loop_crossfade_shape[2] = {0.0f, 0.0f};
    uint8_t perform_adsr_env_a_x[2] = {13u, 13u};
    uint8_t perform_adsr_env_d_x[2] = {38u, 38u};
    uint8_t perform_adsr_env_r_x[2] = {89u, 89u};
    uint8_t perform_adsr_env_s_level[2] = {50u, 50u};
    uint8_t perform_emphasis_row = 0;
    uint8_t perform_process_fx_cursor = 0;
    uint8_t perform_process_fx_order[4] = {0, 1, 2, 3};
    uint8_t perform_process_main_cursor = 2;
    uint16_t perform_process_vol_pct[2] = {100u, 100u};
    bool    perform_process_vol_muted[2] = {false, false};
    float   perform_process_vol_unmuted_level[2] = {1.0f, 1.0f};
    bool    perform_process_detail_active = false;
    bool    perform_process_eq_graph_active = false;
    uint8_t perform_process_detail_param[4] = {0, 0, 0, 0};
    uint8_t fx_field_cursor = 0;
    uint8_t mod_field_cursor = 0;
};
