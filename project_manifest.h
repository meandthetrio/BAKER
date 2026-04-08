#pragma once

#include <cstdint>

#include "sample_edit.h"
#include "macros.h"
#include "mod_matrix.h"

static constexpr uint16_t kProjectManifestVersion = 5;
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
