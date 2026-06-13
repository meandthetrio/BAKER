#pragma once

#include <cstdint>

#include "express_state.h"
#include "sample_edit.h"
#include "macros.h"
#include "mod_matrix.h"

static constexpr uint16_t kProjectManifestVersion = 19;
static constexpr uint8_t kProjectPathMax = 64;
static constexpr uint8_t kProjectSampleLayerCount = 2;

struct ProjectManifestV1
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 1;
    uint16_t reserved = 0;
    char     wav_path[kProjectPathMax] = {};
    SampleEdit edit{};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  pad[3] = {};
};

struct ProjectManifest
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 2;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  pad[3] = {};
};

struct ProjectManifestV3
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 3;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  pad[3] = {};
};

struct ProjectManifestV4
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 4;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  pad[3] = {};
};

struct ProjectManifestV5
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 5;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  pad[3] = {};
};

struct ProjectManifestV6
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 6;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    int16_t  engine_gain_db[kProjectSampleLayerCount] = {0, 0};
    uint8_t  engine_drive_mode[kProjectSampleLayerCount] = {0u, 0u};
    float    engine_filter_cutoff_hz[kProjectSampleLayerCount] = {20000.0f, 20000.0f};
    float    engine_filter_resonance[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  pad[3] = {};
};

struct ProjectManifestV7
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 7;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    int16_t  engine_gain_db[kProjectSampleLayerCount] = {0, 0};
    uint8_t  engine_drive_mode[kProjectSampleLayerCount] = {0u, 0u};
    float    engine_filter_cutoff_hz[kProjectSampleLayerCount] = {20000.0f, 20000.0f};
    float    engine_filter_resonance[kProjectSampleLayerCount] = {0.0f, 0.0f};
    float    engine_layer_master_level[kProjectSampleLayerCount] = {1.0f, 1.0f};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  pad[3] = {};
};

struct ProjectManifestV8
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 8;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    int16_t  engine_gain_db[kProjectSampleLayerCount] = {0, 0};
    uint8_t  engine_drive_mode[kProjectSampleLayerCount] = {0u, 0u};
    float    engine_filter_cutoff_hz[kProjectSampleLayerCount] = {20000.0f, 20000.0f};
    float    engine_filter_resonance[kProjectSampleLayerCount] = {0.0f, 0.0f};
    float    engine_layer_master_level[kProjectSampleLayerCount] = {1.0f, 1.0f};
    uint8_t  fx_order[4] = {0, 2, 3, 1};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  pad[3] = {};
};

struct ProjectSatState
{
    uint8_t sat_on = 0;
    uint8_t sat_mode = 0;
    uint8_t pad[2] = {};
    float   sat_mix = 0.0f;
    float   sat_drive = 0.0f;
    float   sat_bump = 0.5f;
    float   sat_bit_reso = 0.5f;
    float   sat_bit_smpl = 0.5f;
};

struct ProjectManifestV9
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 9;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    int16_t  engine_gain_db[kProjectSampleLayerCount] = {0, 0};
    uint8_t  engine_drive_mode[kProjectSampleLayerCount] = {0u, 0u};
    float    engine_filter_cutoff_hz[kProjectSampleLayerCount] = {20000.0f, 20000.0f};
    float    engine_filter_resonance[kProjectSampleLayerCount] = {0.0f, 0.0f};
    float    engine_layer_master_level[kProjectSampleLayerCount] = {1.0f, 1.0f};
    uint8_t  fx_order[4] = {0, 2, 3, 1};
    ProjectSatState sat{};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  pad[3] = {};
};

struct ProjectEqState
{
    uint8_t eq_on = 1;
    uint8_t pad[3] = {};
    float   eq_mix = 1.0f;
    float   eq_center_norm = 0.5f;
    float   eq_tilt_db = 0.0f;
    float   eq_q = 1.2f;
};

struct ProjectDelayState
{
    uint8_t delay_on = 0;
    uint8_t delay_fader_mode = 0;
    uint8_t pad[2] = {};
    float   delay_mix = 0.0f;
    float   delay_time_l = 0.5f;
    float   delay_time_r = 0.5f;
    float   delay_feedback = 0.5f;
};

struct ProjectReverbState
{
    uint8_t reverb_on = 0;
    uint8_t reverb_fader_mode = 0;
    uint8_t pad[2] = {};
    float   reverb_mix = 0.0f;
    float   reverb_pre = 0.5f;
    float   reverb_damp = 0.5f;
    float   reverb_decay = 0.5f;
    float   reverb_mod = 0.0f;
};

struct ProjectExpressState
{
    static constexpr uint8_t kLayerCount = kProjectSampleLayerCount;
    static constexpr uint8_t kRowCount = 3;

    uint8_t  target[kLayerCount][kRowCount]
        = {{kExpressNone, kExpressNone, kExpressNone},
           {kExpressNone, kExpressNone, kExpressNone}};
    uint16_t min_value[kLayerCount][kRowCount] = {{0u, 0u, 0u}, {0u, 0u, 0u}};
    uint16_t max_value[kLayerCount][kRowCount] = {{0u, 0u, 0u}, {0u, 0u, 0u}};
    uint8_t  poly_porto_voice_limit[kLayerCount] = {kExpressPolyPortoVoicesDefault,
                                                     kExpressPolyPortoVoicesDefault};
    uint16_t poly_porto_slide_ms[kLayerCount] = {kExpressPolyPortoSlideDefaultMs,
                                                  kExpressPolyPortoSlideDefaultMs};
    uint8_t  poly_porto_source_range_semitones[kLayerCount]
        = {kExpressPolyPortoRangeDefaultSemitones, kExpressPolyPortoRangeDefaultSemitones};
    uint8_t  poly_porto_source_mode[kLayerCount]
        = {kExpressPolyPortoSourceClosest, kExpressPolyPortoSourceClosest};
    uint16_t poly_porto_release_ms[kLayerCount] = {kExpressPolyPortoReleaseDefaultMs,
                                                    kExpressPolyPortoReleaseDefaultMs};
};

struct ProjectExpressStateV13Legacy
{
    static constexpr uint8_t kLayerCount = kProjectSampleLayerCount;
    static constexpr uint8_t kRowCount = 3;

    uint8_t  target[kLayerCount][kRowCount]
        = {{kExpressNone, kExpressNone, kExpressNone},
           {kExpressNone, kExpressNone, kExpressNone}};
    uint16_t min_value[kLayerCount][kRowCount] = {{0u, 0u, 0u}, {0u, 0u, 0u}};
    uint16_t max_value[kLayerCount][kRowCount] = {{0u, 0u, 0u}, {0u, 0u, 0u}};
    uint8_t  poly_porto_voice_limit[kLayerCount] = {kExpressPolyPortoVoicesDefault,
                                                     kExpressPolyPortoVoicesDefault};
    uint16_t poly_porto_slide_ms[kLayerCount] = {kExpressPolyPortoSlideDefaultMs,
                                                  kExpressPolyPortoSlideDefaultMs};
    uint8_t  poly_porto_source_range_semitones[kLayerCount]
        = {kExpressPolyPortoRangeDefaultSemitones, kExpressPolyPortoRangeDefaultSemitones};
};

struct ProjectExpressStateV12Legacy
{
    static constexpr uint8_t kLayerCount = kProjectSampleLayerCount;
    static constexpr uint8_t kRowCount = 3;

    uint8_t  target[kLayerCount][kRowCount] = {{0u, 1u, 6u}, {0u, 1u, 6u}};
    uint8_t  pad[2] = {};
    uint16_t min_value[kLayerCount][kRowCount] = {{20u, 0u, 0u}, {20u, 0u, 0u}};
    uint16_t max_value[kLayerCount][kRowCount] = {{20000u, 60u, 100u}, {20000u, 60u, 100u}};
};

struct ProjectManifestV10
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 10;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    int16_t  engine_gain_db[kProjectSampleLayerCount] = {0, 0};
    uint8_t  engine_drive_mode[kProjectSampleLayerCount] = {0u, 0u};
    float    engine_filter_cutoff_hz[kProjectSampleLayerCount] = {20000.0f, 20000.0f};
    float    engine_filter_resonance[kProjectSampleLayerCount] = {0.0f, 0.0f};
    float    engine_layer_master_level[kProjectSampleLayerCount] = {1.0f, 1.0f};
    uint8_t  fx_order[4] = {0, 2, 3, 1};
    ProjectSatState sat{};
    ProjectEqState  eq{};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  pad[3] = {};
};

struct ProjectManifestV11Legacy
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 11;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    int16_t  engine_gain_db[kProjectSampleLayerCount] = {0, 0};
    uint8_t  engine_drive_mode[kProjectSampleLayerCount] = {0u, 0u};
    float    engine_filter_cutoff_hz[kProjectSampleLayerCount] = {20000.0f, 20000.0f};
    float    engine_filter_resonance[kProjectSampleLayerCount] = {0.0f, 0.0f};
    float    engine_layer_master_level[kProjectSampleLayerCount] = {1.0f, 1.0f};
    uint8_t  fx_order[4] = {0, 2, 3, 1};
    ProjectSatState   sat{};
    ProjectEqState    eq{};
    ProjectDelayState delay{};
    ProjectReverbState reverb{};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  pad[3] = {};
};

struct ProjectManifestV12Legacy
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 12;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    int16_t  engine_gain_db[kProjectSampleLayerCount] = {0, 0};
    uint8_t  engine_drive_mode[kProjectSampleLayerCount] = {0u, 0u};
    float    engine_filter_cutoff_hz[kProjectSampleLayerCount] = {20000.0f, 20000.0f};
    float    engine_filter_resonance[kProjectSampleLayerCount] = {0.0f, 0.0f};
    float    engine_layer_master_level[kProjectSampleLayerCount] = {1.0f, 1.0f};
    uint8_t  fx_order[4] = {0, 2, 3, 1};
    ProjectSatState   sat{};
    ProjectEqState    eq{};
    ProjectDelayState delay{};
    ProjectReverbState reverb{};
    ProjectExpressStateV12Legacy express{};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  pad[3] = {};
};

struct ProjectManifestV14Legacy
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 14;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    int16_t  engine_gain_db[kProjectSampleLayerCount] = {0, 0};
    uint8_t  engine_drive_mode[kProjectSampleLayerCount] = {0u, 0u};
    float    engine_filter_cutoff_hz[kProjectSampleLayerCount] = {20000.0f, 20000.0f};
    float    engine_filter_resonance[kProjectSampleLayerCount] = {0.0f, 0.0f};
    float    engine_layer_master_level[kProjectSampleLayerCount] = {1.0f, 1.0f};
    uint8_t  fx_order[4] = {0, 2, 3, 1};
    ProjectSatState   sat{};
    ProjectEqState    eq{};
    ProjectDelayState delay{};
    ProjectReverbState reverb{};
    ProjectExpressState express{};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  express_enabled = 0;
    int8_t   engine_tune_cents[kProjectSampleLayerCount] = {};
};

struct ProjectManifestV15Legacy
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 15;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    int16_t  engine_gain_db[kProjectSampleLayerCount] = {0, 0};
    uint8_t  engine_drive_mode[kProjectSampleLayerCount] = {0u, 0u};
    float    engine_filter_cutoff_hz[kProjectSampleLayerCount] = {20000.0f, 20000.0f};
    float    engine_filter_resonance[kProjectSampleLayerCount] = {0.0f, 0.0f};
    float    engine_layer_master_level[kProjectSampleLayerCount] = {1.0f, 1.0f};
    uint8_t  fx_order[4] = {0, 2, 3, 1};
    ProjectSatState   sat{};
    ProjectEqState    eq{};
    ProjectDelayState delay{};
    ProjectReverbState reverb{};
    ProjectExpressState express{};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  express_enabled = 0;
    int8_t   engine_tune_cents[kProjectSampleLayerCount] = {};
    char     project_name[13] = {};
};

struct ProjectManifestV11
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = kProjectManifestVersion;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    int16_t  engine_gain_db[kProjectSampleLayerCount] = {0, 0};
    uint8_t  engine_drive_mode[kProjectSampleLayerCount] = {0u, 0u};
    float    engine_filter_cutoff_hz[kProjectSampleLayerCount] = {20000.0f, 20000.0f};
    float    engine_filter_resonance[kProjectSampleLayerCount] = {0.0f, 0.0f};
    float    engine_layer_master_level[kProjectSampleLayerCount] = {1.0f, 1.0f};
    uint8_t  fx_order[4] = {0, 2, 3, 1};
    ProjectSatState   sat{};
    ProjectEqState    eq{};
    ProjectDelayState delay{};
    ProjectReverbState reverb{};
    ProjectExpressState express{};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  express_enabled = 0;
    int8_t   engine_tune_cents[kProjectSampleLayerCount] = {};
    char     project_name[13] = {};
    uint8_t  project_style = 0;
    uint8_t  project_style_pad[2] = {};
    float    master_level = 1.0f; // OUTPUT VOL (Settings/Shift page). 0..2 (UNITY=1).
};

// Snapshot of the V11 in-memory layout as it existed at manifest versions
// 16/17/18. Used to read legacy saves whose on-disk size matches this older
// layout (i.e., before master_level was appended at v19). Field-by-field
// copy in the upgrade fn populates the current V11 and defaults master_level.
struct ProjectManifestV18Legacy
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 18u;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    int16_t  engine_gain_db[kProjectSampleLayerCount] = {0, 0};
    uint8_t  engine_drive_mode[kProjectSampleLayerCount] = {0u, 0u};
    float    engine_filter_cutoff_hz[kProjectSampleLayerCount] = {20000.0f, 20000.0f};
    float    engine_filter_resonance[kProjectSampleLayerCount] = {0.0f, 0.0f};
    float    engine_layer_master_level[kProjectSampleLayerCount] = {1.0f, 1.0f};
    uint8_t  fx_order[4] = {0, 2, 3, 1};
    ProjectSatState   sat{};
    ProjectEqState    eq{};
    ProjectDelayState delay{};
    ProjectReverbState reverb{};
    ProjectExpressState express{};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  express_enabled = 0;
    int8_t   engine_tune_cents[kProjectSampleLayerCount] = {};
    char     project_name[13] = {};
    uint8_t  project_style = 0;
    uint8_t  project_style_pad[2] = {};
};

struct ProjectManifestV13Legacy
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 13;
    uint8_t  sample_present_mask = 0;
    uint8_t  reserved = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint8_t  perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
    uint8_t  perform_adsr_loop_sustain[kProjectSampleLayerCount] = {100u, 100u};
    uint16_t perform_adsr_loop_release[kProjectSampleLayerCount] = {50u, 50u};
    float    perform_adsr_loop_crossfade[kProjectSampleLayerCount] = {0.0625f, 0.0625f};
    float    perform_adsr_loop_crossfade_shape[kProjectSampleLayerCount] = {0.0f, 0.0f};
    uint8_t  perform_adsr_env_a_x[kProjectSampleLayerCount] = {13u, 13u};
    uint8_t  perform_adsr_env_d_x[kProjectSampleLayerCount] = {38u, 38u};
    uint8_t  perform_adsr_env_r_x[kProjectSampleLayerCount] = {89u, 89u};
    uint8_t  perform_adsr_env_s_level[kProjectSampleLayerCount] = {50u, 50u};
    int16_t  engine_gain_db[kProjectSampleLayerCount] = {0, 0};
    uint8_t  engine_drive_mode[kProjectSampleLayerCount] = {0u, 0u};
    float    engine_filter_cutoff_hz[kProjectSampleLayerCount] = {20000.0f, 20000.0f};
    float    engine_filter_resonance[kProjectSampleLayerCount] = {0.0f, 0.0f};
    float    engine_layer_master_level[kProjectSampleLayerCount] = {1.0f, 1.0f};
    uint8_t  fx_order[4] = {0, 2, 3, 1};
    ProjectSatState   sat{};
    ProjectEqState    eq{};
    ProjectDelayState delay{};
    ProjectReverbState reverb{};
    ProjectExpressStateV13Legacy express{};
    uint8_t  seq_running = 1;
    uint8_t  plock_apply_enabled = 1;
    uint8_t  lfo_wave = 0;
    uint8_t  macro_sel = 0;
    uint32_t seq_bpm = 120;
    MacroState macro_ui{};
    ModRoute mod_routes[kMaxModRoutes]{};
    uint8_t  mod_route_selected = 0;
    uint8_t  express_enabled = 0;
    uint8_t  pad[2] = {};
};
