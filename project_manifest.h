#pragma once

#include <cstdint>

#include "sample_edit.h"
#include "macros.h"
#include "mod_matrix.h"

static constexpr uint16_t kProjectManifestVersion = 2;
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
    uint16_t version = kProjectManifestVersion;
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
