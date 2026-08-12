#pragma once

#include <cstdint>

#include "express_state.h"
#include "sample_edit.h"
#include "macros.h"
#include "mod_matrix.h"

static constexpr uint16_t kProjectManifestVersion = 25;
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
    // Repurposed from the old `reserved` pad (sizeof unchanged, no version bump):
    // per-layer Attack/Release envelope curve bits. 0 = exponential (default for
    // all pre-existing projects, since the pad was always 0), 1 = logarithmic.
    // bit0=attack L0, bit1=attack L1, bit2=release L0, bit3=release L1.
    uint8_t  adsr_curve_flags = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    // Widened uint8->uint16 at v24 so loop-mode decay can reach 1000 ms.
    uint16_t perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
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
    float    master_level = 1.0f; // OUTPUT VOL (Settings/Shift page). 0..2 (UNITY=1); default unity (0dB).
    // Velocity-mod lanes (appended at v20). Global (per-lane 0/1, not per-layer).
    // target indexes kVelModTargetList; amount -10..+10; threshold 0..127;
    // shape 0=knee/1=gate; threshold_linked applies threshold edits to both.
    uint8_t  velmod_target[2] = {0u, 0u};
    int8_t   velmod_amount[2] = {0, 0};
    uint8_t  velmod_threshold[2] = {0u, 0u};
    uint8_t  velmod_shape[2] = {1u, 1u}; // default gate
    uint8_t  velmod_threshold_linked = 0u;
    // Keyzone FULL/SPLIT mode. Repurposed from velmod_pad[0] (still v20; sizeof
    // unchanged). Pre-existing v20 projects have this byte = 0 → load as FULL.
    uint8_t  perform_keyzone_is_split = 0u;
    // Per-layer emphasis filter mode (0=LP,1=HP,2=BP). Repurposed from the
    // remaining velmod_pad[2] (still v20; sizeof unchanged). Pre-existing v20
    // projects have these bytes = 0 -> load as LP.
    uint8_t  engine_filter_mode[kProjectSampleLayerCount] = {0u, 0u};
    // Velmod source per lane (appended at v21; sizeof grows, hence the bump).
    // 0=>vel 1=<vel 2=>note 3=<note. Pre-v21 projects default to 0 (>vel).
    uint8_t  velmod_source[2] = {0u, 0u};
    // Tape saturator TONE/BIAS (appended at v22; sizeof grows, hence the bump).
    // Kept top-level (not in ProjectSatState) so the shared sub-struct embedded
    // in every frozen VxxLegacy snapshot stays byte-identical. Pre-v22 projects
    // default to tone=0.5 (neutral) and bias=0.0 (no asymmetry).
    float    sat_tone = 0.5f;
    float    sat_bias = 0.0f;
    // Keytrack volume tilt + amount (appended at v23; sizeof grows, hence the
    // bump). tilt is bipolar -kPerformKeytrackTiltMax..+max (0 = flat); amount is
    // the dB floor the full tilt represents, -6..0. UI-only for now. Pre-v23
    // projects default to 0 (flat / no keytrack).
    int8_t   perform_keytrack_tilt = 0;
    int8_t   perform_keytrack_amount_db = 0;
    uint8_t  perform_keytrack_mid_note = 66; // F#4 (0 dB pivot, C3..C6)
    // Added at v24 as padding to make the struct a distinct sizeof from
    // ProjectManifestV23Legacy (widening decay alone was absorbed by alignment
    // padding). Now also carries the EQ tilt-vs-bell flag in **bit 0** (packed
    // here to avoid another version bump); other bits remain reserved/0.
    uint16_t reserved_v24 = 0;
    // Independent low/high EQ shelf bands (appended top-level at v25, NOT into
    // ProjectEqState which is embedded in frozen legacy snapshots). Each is a
    // shelf at its gain, or a filter (lo->HP, hi->LP) when *_is_filter, where the
    // gain becomes resonance (*_q). Pre-v25 projects default to flat shelves.
    float    eq_lo_gain_db   = 0.0f;
    float    eq_lo_cutoff_hz = 120.0f;
    uint8_t  eq_lo_is_filter = 0u;
    float    eq_lo_q         = 0.707f;
    float    eq_hi_gain_db   = 0.0f;
    float    eq_hi_cutoff_hz = 6000.0f;
    uint8_t  eq_hi_is_filter = 0u;
    float    eq_hi_q         = 0.707f;
};

// Snapshot of the current V11 in-memory layout as it existed at manifest version
// 24 — i.e. before the top-level low/high EQ shelf fields were appended at v25.
// Distinct sizeof, so the size-based loader can identify it.
struct ProjectManifestV24Legacy
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 24u;
    uint8_t  sample_present_mask = 0;
    uint8_t  adsr_curve_flags = 0;
    char     wav_path[kProjectSampleLayerCount][kProjectPathMax] = {};
    SampleEdit edit[kProjectSampleLayerCount]{};
    int8_t   engine_tune_semitones[kProjectSampleLayerCount] = {};
    uint8_t  perform_keyzone_lo_note[kProjectSampleLayerCount] = {48u, 48u};
    uint8_t  perform_keyzone_hi_note[kProjectSampleLayerCount] = {60u, 60u};
    uint8_t  perform_adsr_row[kProjectSampleLayerCount] = {1u, 1u};
    uint8_t  engine_play_mode[kProjectSampleLayerCount] = {1u, 1u};
    uint16_t perform_adsr_loop_attack[kProjectSampleLayerCount] = {5u, 5u};
    uint16_t perform_adsr_loop_decay[kProjectSampleLayerCount] = {20u, 20u};
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
    float    master_level = 1.0f;
    uint8_t  velmod_target[2] = {0u, 0u};
    int8_t   velmod_amount[2] = {0, 0};
    uint8_t  velmod_threshold[2] = {0u, 0u};
    uint8_t  velmod_shape[2] = {1u, 1u};
    uint8_t  velmod_threshold_linked = 0u;
    uint8_t  perform_keyzone_is_split = 0u;
    uint8_t  engine_filter_mode[kProjectSampleLayerCount] = {0u, 0u};
    uint8_t  velmod_source[2] = {0u, 0u};
    float    sat_tone = 0.5f;
    float    sat_bias = 0.0f;
    int8_t   perform_keytrack_tilt = 0;
    int8_t   perform_keytrack_amount_db = 0;
    uint8_t  perform_keytrack_mid_note = 66;
    uint16_t reserved_v24 = 0;
};

static_assert(sizeof(ProjectManifestV11) != sizeof(ProjectManifestV24Legacy),
              "v25 appended EQ fields must change the manifest sizeof");

// Snapshot of the current V11 in-memory layout as it existed at manifest version
// 23 — i.e. with perform_adsr_loop_decay still uint8_t (before it was widened to
// uint16_t at v24 to allow a 1000 ms decay). Distinct sizeof from the current
// struct (decay grew by 2 bytes), so the size-based loader can identify it.
struct ProjectManifestV23Legacy
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 23u;
    uint8_t  sample_present_mask = 0;
    uint8_t  adsr_curve_flags = 0;
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
    float    master_level = 1.0f;
    uint8_t  velmod_target[2] = {0u, 0u};
    int8_t   velmod_amount[2] = {0, 0};
    uint8_t  velmod_threshold[2] = {0u, 0u};
    uint8_t  velmod_shape[2] = {1u, 1u};
    uint8_t  velmod_threshold_linked = 0u;
    uint8_t  perform_keyzone_is_split = 0u;
    uint8_t  engine_filter_mode[kProjectSampleLayerCount] = {0u, 0u};
    uint8_t  velmod_source[2] = {0u, 0u};
    float    sat_tone = 0.5f;
    float    sat_bias = 0.0f;
    int8_t   perform_keytrack_tilt = 0;
    int8_t   perform_keytrack_amount_db = 0;
    uint8_t  perform_keytrack_mid_note = 66;
};

static_assert(sizeof(ProjectManifestV11) != sizeof(ProjectManifestV23Legacy),
              "v24 widened decay must change the manifest sizeof so the loader can "
              "distinguish v23 files by size");

// Snapshot of the current V11 in-memory layout as it existed at manifest
// version 22 — i.e. with sat_tone/sat_bias but before perform_keytrack_tilt was
// appended at v23. Read directly into the (larger) current manifest;
// perform_keytrack_tilt keeps its default (0 = flat). Same magic/version
// validity check, distinct sizeof.
struct ProjectManifestV22Legacy
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 22u;
    uint8_t  sample_present_mask = 0;
    uint8_t  adsr_curve_flags = 0;
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
    float    master_level = 1.0f;
    uint8_t  velmod_target[2] = {0u, 0u};
    int8_t   velmod_amount[2] = {0, 0};
    uint8_t  velmod_threshold[2] = {0u, 0u};
    uint8_t  velmod_shape[2] = {1u, 1u};
    uint8_t  velmod_threshold_linked = 0u;
    uint8_t  perform_keyzone_is_split = 0u;
    uint8_t  engine_filter_mode[kProjectSampleLayerCount] = {0u, 0u};
    uint8_t  velmod_source[2] = {0u, 0u};
    float    sat_tone = 0.5f;
    float    sat_bias = 0.0f;
};

static_assert(sizeof(ProjectManifestV11) != sizeof(ProjectManifestV22Legacy),
              "v23 perform_keytrack_tilt append must change manifest sizeof");

// Snapshot of the current V11 in-memory layout as it existed at manifest
// version 20 — i.e. before velmod_source was appended at v21. Read directly
// into the (larger) current manifest; velmod_source keeps its default (>vel).
// Same magic/version validity check, distinct sizeof.
struct ProjectManifestV20Legacy
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 20u;
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
    float    master_level = 1.0f;
    uint8_t  velmod_target[2] = {0u, 0u};
    int8_t   velmod_amount[2] = {0, 0};
    uint8_t  velmod_threshold[2] = {0u, 0u};
    uint8_t  velmod_shape[2] = {1u, 1u};
    uint8_t  velmod_threshold_linked = 0u;
    uint8_t  perform_keyzone_is_split = 0u;
    uint8_t  engine_filter_mode[kProjectSampleLayerCount] = {0u, 0u};
};

// The size-based loader requires the v21 append to actually change sizeof; if
// velmod_source were absorbed into trailing padding, a v20 file would match the
// current-size branch and misread. This guards that.
static_assert(sizeof(ProjectManifestV11) != sizeof(ProjectManifestV20Legacy),
              "v21 velmod_source append must change manifest sizeof");

// Snapshot of the current V11 in-memory layout as it existed at manifest
// version 21 — i.e. with velmod_source but before sat_tone/sat_bias were
// appended at v22. Read directly into the (larger) current manifest;
// sat_tone/sat_bias keep their defaults (neutral tone, no bias).
struct ProjectManifestV21Legacy
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 21u;
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
    float    master_level = 1.0f;
    uint8_t  velmod_target[2] = {0u, 0u};
    int8_t   velmod_amount[2] = {0, 0};
    uint8_t  velmod_threshold[2] = {0u, 0u};
    uint8_t  velmod_shape[2] = {1u, 1u};
    uint8_t  velmod_threshold_linked = 0u;
    uint8_t  perform_keyzone_is_split = 0u;
    uint8_t  engine_filter_mode[kProjectSampleLayerCount] = {0u, 0u};
    uint8_t  velmod_source[2] = {0u, 0u};
};

static_assert(sizeof(ProjectManifestV11) != sizeof(ProjectManifestV21Legacy),
              "v22 sat_tone/sat_bias append must change manifest sizeof");

// Snapshot of the current V11 in-memory layout as it existed at manifest
// version 19 — i.e. before the velmod tail was appended at v20. Read directly
// into the (larger) current manifest; the appended velmod fields keep their
// struct defaults. Same magic/version validity check, distinct sizeof.
struct ProjectManifestV19Legacy
{
    char     magic[4] = {'A', 'K', 'P', 'J'};
    uint16_t version = 19u;
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
    float    master_level = 1.0f;
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
