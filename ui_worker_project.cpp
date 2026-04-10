#include "ui_worker_internal.h"

#include "sample_edit.h"
#include "params.h"
#include "macros.h"
#include "mod_matrix.h"
#include "tilt_eq.h"

#include <cstdio>
#include <cstring>

// Project slot / path helpers
static void SetProjectStatus(AppState& app, const char* msg)
{
    if(!msg)
    {
        app.project.project_status[0] = '\0';
        return;
    }
    std::snprintf(app.project.project_status, sizeof(app.project.project_status), "%s", msg);
}

static uint8_t ClampProjectSlotIndex(uint8_t slot)
{
    return (slot < kProjectSlotCount) ? slot : 0u;
}

uint8_t RequestedProjectSlot(const AppState& app)
{
    return ClampProjectSlotIndex(static_cast<uint8_t>(app.worker.ui_req_arg0));
}

static bool MakeProjectSlotFilename(char* out, size_t n, uint8_t slot, const char* ext)
{
    if(!out || n == 0 || !ext)
        return false;

    const uint8_t slot_num = static_cast<uint8_t>(ClampProjectSlotIndex(slot) + 1u);
    const int r = std::snprintf(out, n, "PROJECT%02u.%s", slot_num, ext);
    if(r < 0 || r >= static_cast<int>(n))
    {
        out[n - 1] = '\0';
        return false;
    }
    return true;
}

static bool MakeProjectSlotPath(char* out, size_t n, const char* base, uint8_t slot, const char* ext)
{
    char name[20];
    if(!MakeProjectSlotFilename(name, sizeof(name), slot, ext))
        return false;
    return MakePath(out, n, base, name);
}

void SetProjectSlotStatus(AppState& app, uint8_t slot, const char* msg)
{
    char status[sizeof(app.project.project_status)];
    const uint8_t slot_num = static_cast<uint8_t>(ClampProjectSlotIndex(slot) + 1u);
    std::snprintf(status, sizeof(status), "P%02u %s", slot_num, msg ? msg : "");
    SetProjectStatus(app, status);
}

// Manifest validity helpers
static bool ProjectManifestValid(const ProjectManifestV1& m)
{
    return std::memcmp(m.magic, "AKPJ", 4) == 0 && m.version == 1u;
}

static bool ProjectManifestValid(const ProjectManifest& m)
{
    return std::memcmp(m.magic, "AKPJ", 4) == 0 && m.version == 2u;
}

static bool ProjectManifestValid(const ProjectManifestV3& m)
{
    return std::memcmp(m.magic, "AKPJ", 4) == 0 && m.version == 3u;
}

static bool ProjectManifestValid(const ProjectManifestV4& m)
{
    return std::memcmp(m.magic, "AKPJ", 4) == 0 && m.version == 4u;
}

static bool ProjectManifestValid(const ProjectManifestV5& m)
{
    return std::memcmp(m.magic, "AKPJ", 4) == 0
           && m.version == 5u;
}

static bool ProjectManifestValid(const ProjectManifestV6& m)
{
    return std::memcmp(m.magic, "AKPJ", 4) == 0
           && m.version == 6u;
}

static bool ProjectManifestValid(const ProjectManifestV7& m)
{
    return std::memcmp(m.magic, "AKPJ", 4) == 0
           && m.version == 7u;
}

static bool ProjectManifestValid(const ProjectManifestV8& m)
{
    return std::memcmp(m.magic, "AKPJ", 4) == 0
           && m.version == 8u;
}

static bool ProjectManifestValid(const ProjectManifestV9& m)
{
    return std::memcmp(m.magic, "AKPJ", 4) == 0
           && m.version == 9u;
}

static bool ProjectManifestValid(const ProjectManifestV10& m)
{
    return std::memcmp(m.magic, "AKPJ", 4) == 0
           && m.version == kProjectManifestVersion;
}

// Manifest upgrade helpers
static void ProjectManifestUpgrade(ProjectManifest& dst, const ProjectManifestV1& src)
{
    dst = ProjectManifest{};
    std::snprintf(dst.wav_path[0], sizeof(dst.wav_path[0]), "%s", src.wav_path);
    dst.edit[0] = src.edit;
    dst.sample_present_mask = (src.wav_path[0] != '\0') ? 0x01u : 0u;
    dst.seq_running = src.seq_running;
    dst.plock_apply_enabled = src.plock_apply_enabled;
    dst.lfo_wave = src.lfo_wave;
    dst.macro_sel = src.macro_sel;
    dst.seq_bpm = src.seq_bpm;
    dst.macro_ui = src.macro_ui;
    for(size_t i = 0; i < kMaxModRoutes; ++i)
        dst.mod_routes[i] = src.mod_routes[i];
    dst.mod_route_selected = src.mod_route_selected;
}

static void ProjectManifestUpgrade(ProjectManifestV3& dst, const ProjectManifest& src)
{
    dst = ProjectManifestV3{};
    dst.sample_present_mask = src.sample_present_mask;
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        std::snprintf(dst.wav_path[slot], sizeof(dst.wav_path[slot]), "%s", src.wav_path[slot]);
        dst.edit[slot] = src.edit[slot];
        dst.engine_tune_semitones[slot] = 0;
    }
    dst.seq_running = src.seq_running;
    dst.plock_apply_enabled = src.plock_apply_enabled;
    dst.lfo_wave = src.lfo_wave;
    dst.macro_sel = src.macro_sel;
    dst.seq_bpm = src.seq_bpm;
    dst.macro_ui = src.macro_ui;
    for(size_t i = 0; i < kMaxModRoutes; ++i)
        dst.mod_routes[i] = src.mod_routes[i];
    dst.mod_route_selected = src.mod_route_selected;
}

static void ProjectManifestUpgrade(ProjectManifestV4& dst, const ProjectManifestV3& src)
{
    dst = ProjectManifestV4{};
    dst.sample_present_mask = src.sample_present_mask;
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        std::snprintf(dst.wav_path[slot], sizeof(dst.wav_path[slot]), "%s", src.wav_path[slot]);
        dst.edit[slot] = src.edit[slot];
        dst.engine_tune_semitones[slot] = src.engine_tune_semitones[slot];
        dst.perform_keyzone_lo_note[slot] = 48u;
        dst.perform_keyzone_hi_note[slot] = 60u;
    }
    dst.seq_running = src.seq_running;
    dst.plock_apply_enabled = src.plock_apply_enabled;
    dst.lfo_wave = src.lfo_wave;
    dst.macro_sel = src.macro_sel;
    dst.seq_bpm = src.seq_bpm;
    dst.macro_ui = src.macro_ui;
    for(size_t i = 0; i < kMaxModRoutes; ++i)
        dst.mod_routes[i] = src.mod_routes[i];
    dst.mod_route_selected = src.mod_route_selected;
}

static void ProjectManifestUpgrade(ProjectManifestV5& dst, const ProjectManifestV4& src)
{
    dst = ProjectManifestV5{};
    dst.sample_present_mask = src.sample_present_mask;
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        std::snprintf(dst.wav_path[slot], sizeof(dst.wav_path[slot]), "%s", src.wav_path[slot]);
        dst.edit[slot] = src.edit[slot];
        dst.engine_tune_semitones[slot] = src.engine_tune_semitones[slot];
        dst.perform_keyzone_lo_note[slot] = src.perform_keyzone_lo_note[slot];
        dst.perform_keyzone_hi_note[slot] = src.perform_keyzone_hi_note[slot];
        dst.perform_adsr_row[slot] = 1u;
        dst.engine_play_mode[slot] = 1u;
        dst.perform_adsr_loop_attack[slot] = 5u;
        dst.perform_adsr_loop_decay[slot] = 20u;
        dst.perform_adsr_loop_sustain[slot] = 100u;
        dst.perform_adsr_loop_release[slot] = 50u;
        dst.perform_adsr_loop_crossfade[slot] = 0.0625f;
        dst.perform_adsr_loop_crossfade_shape[slot] = 0.0f;
        dst.perform_adsr_env_a_x[slot] = 13u;
        dst.perform_adsr_env_d_x[slot] = 38u;
        dst.perform_adsr_env_r_x[slot] = 89u;
        dst.perform_adsr_env_s_level[slot] = 50u;
    }
    dst.seq_running = src.seq_running;
    dst.plock_apply_enabled = src.plock_apply_enabled;
    dst.lfo_wave = src.lfo_wave;
    dst.macro_sel = src.macro_sel;
    dst.seq_bpm = src.seq_bpm;
    dst.macro_ui = src.macro_ui;
    for(size_t i = 0; i < kMaxModRoutes; ++i)
        dst.mod_routes[i] = src.mod_routes[i];
    dst.mod_route_selected = src.mod_route_selected;
}

static void ProjectManifestUpgrade(ProjectManifestV6& dst, const ProjectManifestV5& src)
{
    dst = ProjectManifestV6{};
    dst.sample_present_mask = src.sample_present_mask;
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        std::snprintf(dst.wav_path[slot], sizeof(dst.wav_path[slot]), "%s", src.wav_path[slot]);
        dst.edit[slot] = src.edit[slot];
        dst.engine_tune_semitones[slot] = src.engine_tune_semitones[slot];
        dst.perform_keyzone_lo_note[slot] = src.perform_keyzone_lo_note[slot];
        dst.perform_keyzone_hi_note[slot] = src.perform_keyzone_hi_note[slot];
        dst.perform_adsr_row[slot] = src.perform_adsr_row[slot];
        dst.engine_play_mode[slot] = src.engine_play_mode[slot];
        dst.perform_adsr_loop_attack[slot] = src.perform_adsr_loop_attack[slot];
        dst.perform_adsr_loop_decay[slot] = src.perform_adsr_loop_decay[slot];
        dst.perform_adsr_loop_sustain[slot] = src.perform_adsr_loop_sustain[slot];
        dst.perform_adsr_loop_release[slot] = src.perform_adsr_loop_release[slot];
        dst.perform_adsr_loop_crossfade[slot] = src.perform_adsr_loop_crossfade[slot];
        dst.perform_adsr_loop_crossfade_shape[slot] = src.perform_adsr_loop_crossfade_shape[slot];
        dst.perform_adsr_env_a_x[slot] = src.perform_adsr_env_a_x[slot];
        dst.perform_adsr_env_d_x[slot] = src.perform_adsr_env_d_x[slot];
        dst.perform_adsr_env_r_x[slot] = src.perform_adsr_env_r_x[slot];
        dst.perform_adsr_env_s_level[slot] = src.perform_adsr_env_s_level[slot];
        dst.engine_gain_db[slot] = 0;
        dst.engine_drive_mode[slot] = 0u;
        dst.engine_filter_cutoff_hz[slot] = 20000.0f;
        dst.engine_filter_resonance[slot] = 0.0f;
    }
    dst.seq_running = src.seq_running;
    dst.plock_apply_enabled = src.plock_apply_enabled;
    dst.lfo_wave = src.lfo_wave;
    dst.macro_sel = src.macro_sel;
    dst.seq_bpm = src.seq_bpm;
    dst.macro_ui = src.macro_ui;
    for(size_t i = 0; i < kMaxModRoutes; ++i)
        dst.mod_routes[i] = src.mod_routes[i];
    dst.mod_route_selected = src.mod_route_selected;
}

static void ProjectManifestUpgrade(ProjectManifestV7& dst, const ProjectManifestV6& src)
{
    dst = ProjectManifestV7{};
    dst.sample_present_mask = src.sample_present_mask;
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        std::snprintf(dst.wav_path[slot], sizeof(dst.wav_path[slot]), "%s", src.wav_path[slot]);
        dst.edit[slot] = src.edit[slot];
        dst.engine_tune_semitones[slot] = src.engine_tune_semitones[slot];
        dst.perform_keyzone_lo_note[slot] = src.perform_keyzone_lo_note[slot];
        dst.perform_keyzone_hi_note[slot] = src.perform_keyzone_hi_note[slot];
        dst.perform_adsr_row[slot] = src.perform_adsr_row[slot];
        dst.engine_play_mode[slot] = src.engine_play_mode[slot];
        dst.perform_adsr_loop_attack[slot] = src.perform_adsr_loop_attack[slot];
        dst.perform_adsr_loop_decay[slot] = src.perform_adsr_loop_decay[slot];
        dst.perform_adsr_loop_sustain[slot] = src.perform_adsr_loop_sustain[slot];
        dst.perform_adsr_loop_release[slot] = src.perform_adsr_loop_release[slot];
        dst.perform_adsr_loop_crossfade[slot] = src.perform_adsr_loop_crossfade[slot];
        dst.perform_adsr_loop_crossfade_shape[slot] = src.perform_adsr_loop_crossfade_shape[slot];
        dst.perform_adsr_env_a_x[slot] = src.perform_adsr_env_a_x[slot];
        dst.perform_adsr_env_d_x[slot] = src.perform_adsr_env_d_x[slot];
        dst.perform_adsr_env_r_x[slot] = src.perform_adsr_env_r_x[slot];
        dst.perform_adsr_env_s_level[slot] = src.perform_adsr_env_s_level[slot];
        dst.engine_gain_db[slot] = src.engine_gain_db[slot];
        dst.engine_drive_mode[slot] = src.engine_drive_mode[slot];
        dst.engine_filter_cutoff_hz[slot] = src.engine_filter_cutoff_hz[slot];
        dst.engine_filter_resonance[slot] = src.engine_filter_resonance[slot];
        dst.engine_layer_master_level[slot] = 1.0f;
    }
    dst.seq_running = src.seq_running;
    dst.plock_apply_enabled = src.plock_apply_enabled;
    dst.lfo_wave = src.lfo_wave;
    dst.macro_sel = src.macro_sel;
    dst.seq_bpm = src.seq_bpm;
    dst.macro_ui = src.macro_ui;
    for(size_t i = 0; i < kMaxModRoutes; ++i)
        dst.mod_routes[i] = src.mod_routes[i];
    dst.mod_route_selected = src.mod_route_selected;
}

static void ProjectManifestUpgrade(ProjectManifestV8& dst, const ProjectManifestV7& src)
{
    dst = ProjectManifestV8{};
    dst.sample_present_mask = src.sample_present_mask;
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        std::snprintf(dst.wav_path[slot], sizeof(dst.wav_path[slot]), "%s", src.wav_path[slot]);
        dst.edit[slot] = src.edit[slot];
        dst.engine_tune_semitones[slot] = src.engine_tune_semitones[slot];
        dst.perform_keyzone_lo_note[slot] = src.perform_keyzone_lo_note[slot];
        dst.perform_keyzone_hi_note[slot] = src.perform_keyzone_hi_note[slot];
        dst.perform_adsr_row[slot] = src.perform_adsr_row[slot];
        dst.engine_play_mode[slot] = src.engine_play_mode[slot];
        dst.perform_adsr_loop_attack[slot] = src.perform_adsr_loop_attack[slot];
        dst.perform_adsr_loop_decay[slot] = src.perform_adsr_loop_decay[slot];
        dst.perform_adsr_loop_sustain[slot] = src.perform_adsr_loop_sustain[slot];
        dst.perform_adsr_loop_release[slot] = src.perform_adsr_loop_release[slot];
        dst.perform_adsr_loop_crossfade[slot] = src.perform_adsr_loop_crossfade[slot];
        dst.perform_adsr_loop_crossfade_shape[slot] = src.perform_adsr_loop_crossfade_shape[slot];
        dst.perform_adsr_env_a_x[slot] = src.perform_adsr_env_a_x[slot];
        dst.perform_adsr_env_d_x[slot] = src.perform_adsr_env_d_x[slot];
        dst.perform_adsr_env_r_x[slot] = src.perform_adsr_env_r_x[slot];
        dst.perform_adsr_env_s_level[slot] = src.perform_adsr_env_s_level[slot];
        dst.engine_gain_db[slot] = src.engine_gain_db[slot];
        dst.engine_drive_mode[slot] = src.engine_drive_mode[slot];
        dst.engine_filter_cutoff_hz[slot] = src.engine_filter_cutoff_hz[slot];
        dst.engine_filter_resonance[slot] = src.engine_filter_resonance[slot];
        dst.engine_layer_master_level[slot] = src.engine_layer_master_level[slot];
    }
    dst.seq_running = src.seq_running;
    dst.plock_apply_enabled = src.plock_apply_enabled;
    dst.lfo_wave = src.lfo_wave;
    dst.macro_sel = src.macro_sel;
    dst.seq_bpm = src.seq_bpm;
    dst.macro_ui = src.macro_ui;
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = static_cast<uint8_t>(i);
    for(size_t i = 0; i < kMaxModRoutes; ++i)
        dst.mod_routes[i] = src.mod_routes[i];
    dst.mod_route_selected = src.mod_route_selected;
}

static void ProjectManifestUpgrade(ProjectManifestV9& dst, const ProjectManifestV8& src)
{
    dst = ProjectManifestV9{};
    dst.sample_present_mask = src.sample_present_mask;
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        std::snprintf(dst.wav_path[slot], sizeof(dst.wav_path[slot]), "%s", src.wav_path[slot]);
        dst.edit[slot] = src.edit[slot];
        dst.engine_tune_semitones[slot] = src.engine_tune_semitones[slot];
        dst.perform_keyzone_lo_note[slot] = src.perform_keyzone_lo_note[slot];
        dst.perform_keyzone_hi_note[slot] = src.perform_keyzone_hi_note[slot];
        dst.perform_adsr_row[slot] = src.perform_adsr_row[slot];
        dst.engine_play_mode[slot] = src.engine_play_mode[slot];
        dst.perform_adsr_loop_attack[slot] = src.perform_adsr_loop_attack[slot];
        dst.perform_adsr_loop_decay[slot] = src.perform_adsr_loop_decay[slot];
        dst.perform_adsr_loop_sustain[slot] = src.perform_adsr_loop_sustain[slot];
        dst.perform_adsr_loop_release[slot] = src.perform_adsr_loop_release[slot];
        dst.perform_adsr_loop_crossfade[slot] = src.perform_adsr_loop_crossfade[slot];
        dst.perform_adsr_loop_crossfade_shape[slot] = src.perform_adsr_loop_crossfade_shape[slot];
        dst.perform_adsr_env_a_x[slot] = src.perform_adsr_env_a_x[slot];
        dst.perform_adsr_env_d_x[slot] = src.perform_adsr_env_d_x[slot];
        dst.perform_adsr_env_r_x[slot] = src.perform_adsr_env_r_x[slot];
        dst.perform_adsr_env_s_level[slot] = src.perform_adsr_env_s_level[slot];
        dst.engine_gain_db[slot] = src.engine_gain_db[slot];
        dst.engine_drive_mode[slot] = src.engine_drive_mode[slot];
        dst.engine_filter_cutoff_hz[slot] = src.engine_filter_cutoff_hz[slot];
        dst.engine_filter_resonance[slot] = src.engine_filter_resonance[slot];
        dst.engine_layer_master_level[slot] = src.engine_layer_master_level[slot];
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.seq_running = src.seq_running;
    dst.plock_apply_enabled = src.plock_apply_enabled;
    dst.lfo_wave = src.lfo_wave;
    dst.macro_sel = src.macro_sel;
    dst.seq_bpm = src.seq_bpm;
    dst.macro_ui = src.macro_ui;
    for(size_t i = 0; i < kMaxModRoutes; ++i)
        dst.mod_routes[i] = src.mod_routes[i];
    dst.mod_route_selected = src.mod_route_selected;
}

static void ProjectManifestUpgrade(ProjectManifestV10& dst, const ProjectManifestV9& src)
{
    dst = ProjectManifestV10{};
    dst.sample_present_mask = src.sample_present_mask;
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        std::snprintf(dst.wav_path[slot], sizeof(dst.wav_path[slot]), "%s", src.wav_path[slot]);
        dst.edit[slot] = src.edit[slot];
        dst.engine_tune_semitones[slot] = src.engine_tune_semitones[slot];
        dst.perform_keyzone_lo_note[slot] = src.perform_keyzone_lo_note[slot];
        dst.perform_keyzone_hi_note[slot] = src.perform_keyzone_hi_note[slot];
        dst.perform_adsr_row[slot] = src.perform_adsr_row[slot];
        dst.engine_play_mode[slot] = src.engine_play_mode[slot];
        dst.perform_adsr_loop_attack[slot] = src.perform_adsr_loop_attack[slot];
        dst.perform_adsr_loop_decay[slot] = src.perform_adsr_loop_decay[slot];
        dst.perform_adsr_loop_sustain[slot] = src.perform_adsr_loop_sustain[slot];
        dst.perform_adsr_loop_release[slot] = src.perform_adsr_loop_release[slot];
        dst.perform_adsr_loop_crossfade[slot] = src.perform_adsr_loop_crossfade[slot];
        dst.perform_adsr_loop_crossfade_shape[slot] = src.perform_adsr_loop_crossfade_shape[slot];
        dst.perform_adsr_env_a_x[slot] = src.perform_adsr_env_a_x[slot];
        dst.perform_adsr_env_d_x[slot] = src.perform_adsr_env_d_x[slot];
        dst.perform_adsr_env_r_x[slot] = src.perform_adsr_env_r_x[slot];
        dst.perform_adsr_env_s_level[slot] = src.perform_adsr_env_s_level[slot];
        dst.engine_gain_db[slot] = src.engine_gain_db[slot];
        dst.engine_drive_mode[slot] = src.engine_drive_mode[slot];
        dst.engine_filter_cutoff_hz[slot] = src.engine_filter_cutoff_hz[slot];
        dst.engine_filter_resonance[slot] = src.engine_filter_resonance[slot];
        dst.engine_layer_master_level[slot] = src.engine_layer_master_level[slot];
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.sat = src.sat;
    dst.seq_running = src.seq_running;
    dst.plock_apply_enabled = src.plock_apply_enabled;
    dst.lfo_wave = src.lfo_wave;
    dst.macro_sel = src.macro_sel;
    dst.seq_bpm = src.seq_bpm;
    dst.macro_ui = src.macro_ui;
    for(size_t i = 0; i < kMaxModRoutes; ++i)
        dst.mod_routes[i] = src.mod_routes[i];
    dst.mod_route_selected = src.mod_route_selected;
}

static bool ProjectManifestHasLayer(const ProjectManifestV10& m, uint8_t layer)
{
    if(layer >= kProjectSampleLayerCount)
        return false;
    const uint8_t bit = static_cast<uint8_t>(1u << layer);
    return (m.sample_present_mask & bit) != 0u && m.wav_path[layer][0] != '\0';
}

// Project clamp helpers
static float ClampProjectFloat(float value, float lo, float hi)
{
    if(value < lo)
        value = lo;
    if(value > hi)
        value = hi;
    return value;
}

static int8_t ClampProjectTune(int value)
{
    if(value < -24)
        value = -24;
    if(value > 24)
        value = 24;
    return static_cast<int8_t>(value);
}

static uint8_t ClampProjectMidiNote(int value)
{
    if(value < 0)
        value = 0;
    if(value > 127)
        value = 127;
    return static_cast<uint8_t>(value);
}

static void ClampProjectKeyzoneRange(uint8_t& lo, uint8_t& hi)
{
    lo = ClampProjectMidiNote(lo);
    hi = ClampProjectMidiNote(hi);
    if(lo > hi)
        hi = lo;
}

static uint8_t ClampProjectAdsrRow(int value)
{
    if(value < 0)
        value = 0;
    if(value > 2)
        value = 2;
    return static_cast<uint8_t>(value);
}

static uint8_t ClampProjectPlayMode(int value)
{
    return (value != 0) ? 1u : 0u;
}

static int16_t ClampProjectEngineGainDb(int value)
{
    if(value < 0)
        value = 0;
    if(value > 60)
        value = 60;
    return static_cast<int16_t>(value);
}

static uint8_t ClampProjectDriveMode(int value)
{
    return (value != 0) ? 1u : 0u;
}

static uint8_t ClampProjectSatMode(int value)
{
    return (value != 0) ? 1u : 0u;
}

static float ClampProjectFilterCutoffHz(float value)
{
    if(value < 20.0f)
        value = 20.0f;
    if(value > 20000.0f)
        value = 20000.0f;
    return value;
}

static void ClampProjectAdsrGraph(uint8_t& a_x, uint8_t& d_x, uint8_t& r_x, uint8_t& s_level)
{
    constexpr int kMinGap = 6;
    int a = ClampProjectMidiNote(a_x);
    int d = ClampProjectMidiNote(d_x);
    int r = ClampProjectMidiNote(r_x);
    int s = ClampProjectMidiNote(s_level);

    if(a > 100 - (2 * kMinGap))
        a = 100 - (2 * kMinGap);
    d = (d < a + kMinGap) ? (a + kMinGap) : d;
    if(d > 100 - kMinGap)
        d = 100 - kMinGap;
    r = (r < d + kMinGap) ? (d + kMinGap) : r;
    if(r > 100)
        r = 100;
    s = (s < 0) ? 0 : ((s > 100) ? 100 : s);

    a_x = static_cast<uint8_t>(a);
    d_x = static_cast<uint8_t>(d);
    r_x = static_cast<uint8_t>(r);
    s_level = static_cast<uint8_t>(s);
}

// Project sanitize helpers
static void ClampProjectSatState(ProjectSatState& sat)
{
    auto clamp01 = [](float value) -> float
    {
        if(value < 0.0f)
            value = 0.0f;
        if(value > 1.0f)
            value = 1.0f;
        return value;
    };

    sat.sat_on = (sat.sat_on != 0u) ? 1u : 0u;
    sat.sat_mode = ClampProjectSatMode(sat.sat_mode);
    sat.sat_mix = clamp01(sat.sat_mix);
    sat.sat_drive = clamp01(sat.sat_drive);
    sat.sat_bump = clamp01(sat.sat_bump);
    sat.sat_bit_reso = clamp01(sat.sat_bit_reso);
    sat.sat_bit_smpl = clamp01(sat.sat_bit_smpl);
}

static void ClampProjectEqState(ProjectEqState& eq)
{
    auto clamp01 = [](float value) -> float
    {
        if(value < 0.0f)
            value = 0.0f;
        if(value > 1.0f)
            value = 1.0f;
        return value;
    };
    auto clamp_eq_tilt = [](float value) -> float
    {
        if(value < -kTiltEqTiltMaxDb)
            value = -kTiltEqTiltMaxDb;
        if(value > kTiltEqTiltMaxDb)
            value = kTiltEqTiltMaxDb;
        return value;
    };
    auto clamp_eq_q = [](float value) -> float
    {
        if(value < kTiltEqQMin)
            value = kTiltEqQMin;
        if(value > kTiltEqQMax)
            value = kTiltEqQMax;
        return value;
    };

    eq.eq_on = (eq.eq_on != 0u) ? 1u : 0u;
    eq.eq_mix = clamp01(eq.eq_mix);
    eq.eq_center_norm = clamp01(eq.eq_center_norm);
    eq.eq_tilt_db = clamp_eq_tilt(eq.eq_tilt_db);
    eq.eq_q = clamp_eq_q(eq.eq_q);
}

static void SanitizeProjectFxOrder(uint8_t* fx_order)
{
    if(!fx_order)
        return;

    bool seen[4] = {false, false, false, false};
    uint8_t next_missing = 0u;
    for(uint8_t i = 0; i < 4; ++i)
    {
        const uint8_t fx = fx_order[i];
        if(fx < 4u && !seen[fx])
        {
            seen[fx] = true;
            continue;
        }

        while(next_missing < 4u && seen[next_missing])
            ++next_missing;
        fx_order[i] = (next_missing < 4u) ? next_missing : static_cast<uint8_t>(i);
        if(fx_order[i] < 4u)
            seen[fx_order[i]] = true;
    }
}

// Project publish / UI sync helpers
static void PublishProjectPerformParams(Params& params,
                                        const AppState& app,
                                        const float* process_layer_master_level = nullptr,
                                        const uint8_t* process_fx_order = nullptr,
                                        const ProjectSatState* process_sat_state = nullptr,
                                        const ProjectEqState* process_eq_state = nullptr,
                                        const float* emphasis_cutoff_hz = nullptr,
                                        const float* emphasis_resonance = nullptr)
{
    PerformParamsTargets& t = params.EditTargets();
    if(process_fx_order)
    {
        for(uint8_t i = 0; i < 4; ++i)
            t.fx_order[i] = process_fx_order[i];
        SanitizeProjectFxOrder(t.fx_order);
    }
    if(process_sat_state)
    {
        t.sat_on = (process_sat_state->sat_on != 0u);
        t.sat_mode = ClampProjectSatMode(process_sat_state->sat_mode);
        t.sat_mix = ClampProjectFloat(process_sat_state->sat_mix, 0.0f, 1.0f);
        t.sat_drive = ClampProjectFloat(process_sat_state->sat_drive, 0.0f, 1.0f);
        t.sat_bump = ClampProjectFloat(process_sat_state->sat_bump, 0.0f, 1.0f);
        t.sat_bit_reso = ClampProjectFloat(process_sat_state->sat_bit_reso, 0.0f, 1.0f);
        t.sat_bit_smpl = ClampProjectFloat(process_sat_state->sat_bit_smpl, 0.0f, 1.0f);
    }
    if(process_eq_state)
    {
        t.eq_on = (process_eq_state->eq_on != 0u);
        t.eq_mix = ClampProjectFloat(process_eq_state->eq_mix, 0.0f, 1.0f);
        t.eq_center_norm = ClampProjectFloat(process_eq_state->eq_center_norm, 0.0f, 1.0f);
        if(process_eq_state->eq_tilt_db < -kTiltEqTiltMaxDb)
            t.eq_tilt_db = -kTiltEqTiltMaxDb;
        else if(process_eq_state->eq_tilt_db > kTiltEqTiltMaxDb)
            t.eq_tilt_db = kTiltEqTiltMaxDb;
        else
            t.eq_tilt_db = process_eq_state->eq_tilt_db;
        if(process_eq_state->eq_q < kTiltEqQMin)
            t.eq_q = kTiltEqQMin;
        else if(process_eq_state->eq_q > kTiltEqQMax)
            t.eq_q = kTiltEqQMax;
        else
            t.eq_q = process_eq_state->eq_q;
    }
    for(uint8_t layer = 0; layer < kProjectSampleLayerCount; ++layer)
    {
        t.engine_tune_semitones[layer] = static_cast<float>(app.engine.engine_tune_semitones[layer]);
        if(process_layer_master_level)
        {
            t.engine_layer_master_level[layer]
                = ClampProjectFloat(process_layer_master_level[layer], 0.0f, 2.0f);
        }
        t.engine_gain_db[layer] = static_cast<float>(app.engine.engine_gain_db[layer]);
        t.engine_drive_mode[layer] = ClampProjectDriveMode(app.engine.engine_drive_mode[layer]);
        if(emphasis_cutoff_hz)
            t.engine_filter_cutoff_hz[layer] = ClampProjectFilterCutoffHz(emphasis_cutoff_hz[layer]);
        if(emphasis_resonance)
            t.engine_filter_resonance[layer] = ClampProjectFloat(emphasis_resonance[layer], 0.0f, 1.0f);
        t.perform_keyzone_lo_note[layer] = app.perform.perform_keyzone_lo_note[layer];
        t.perform_keyzone_hi_note[layer] = app.perform.perform_keyzone_hi_note[layer];
        t.engine_loop_mode[layer] = (app.engine.engine_play_mode[layer] != 0);
        t.engine_loop_attack_ms[layer] = static_cast<float>(app.perform.perform_adsr_loop_attack[layer]);
        t.engine_loop_decay_ms[layer] = static_cast<float>(app.perform.perform_adsr_loop_decay[layer]);
        t.engine_loop_sustain_level[layer]
            = static_cast<float>(app.perform.perform_adsr_loop_sustain[layer]) * 0.01f;
        t.engine_loop_release_ms[layer] = static_cast<float>(app.perform.perform_adsr_loop_release[layer]);
        t.engine_loop_crossfade_amount[layer] = app.perform.perform_adsr_loop_crossfade[layer];
        t.engine_loop_crossfade_shape[layer] = app.perform.perform_adsr_loop_crossfade_shape[layer];
    }
    params.PublishTargets();
}

static void SyncProjectProcessVolumeUiState(AppState& app, const float* process_layer_master_level)
{
    if(!process_layer_master_level)
        return;

    for(uint8_t layer = 0; layer < kProjectSampleLayerCount; ++layer)
    {
        const float level = ClampProjectFloat(process_layer_master_level[layer], 0.0f, 2.0f);
        app.perform.perform_process_vol_muted[layer] = false;
        app.perform.perform_process_vol_unmuted_level[layer] = level;
        app.perform.perform_process_vol_pct[layer] = static_cast<uint16_t>(level * 100.0f + 0.5f);
    }
}

static void SyncProjectProcessFxOrderUiState(AppState& app, const uint8_t* process_fx_order)
{
    if(!process_fx_order)
        return;

    for(uint8_t i = 0; i < 4; ++i)
        app.perform.perform_process_fx_order[i] = process_fx_order[i];
    SanitizeProjectFxOrder(app.perform.perform_process_fx_order);
}

static void CollectProjectLayerState(ProjectManifestV10& manifest,
                                     const AppState& app,
                                     const PerformParamsTargets& targets)
{
    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
    {
        const Sample& sample = app.sd_slots[slot];
        const char* path = app.engine.engine_sample_path[slot];
        if(sample.pcm == nullptr || sample.length == 0 || !path || path[0] == '\0')
            continue;

        const uint8_t bit = static_cast<uint8_t>(1u << slot);
        manifest.sample_present_mask |= bit;
        std::snprintf(manifest.wav_path[slot], sizeof(manifest.wav_path[slot]), "%s", path);

        SampleEdit edit = app.sd_edit_slots[slot];
        SampleEdit_Clamp(edit, sample.length);
        manifest.edit[slot] = edit;
        manifest.engine_tune_semitones[slot] = ClampProjectTune(app.engine.engine_tune_semitones[slot]);
        manifest.perform_keyzone_lo_note[slot] = app.perform.perform_keyzone_lo_note[slot];
        manifest.perform_keyzone_hi_note[slot] = app.perform.perform_keyzone_hi_note[slot];
        manifest.perform_adsr_row[slot] = ClampProjectAdsrRow(app.perform.perform_adsr_row[slot]);
        manifest.engine_play_mode[slot] = ClampProjectPlayMode(app.engine.engine_play_mode[slot]);
        manifest.perform_adsr_loop_attack[slot] = app.perform.perform_adsr_loop_attack[slot];
        manifest.perform_adsr_loop_decay[slot] = app.perform.perform_adsr_loop_decay[slot];
        manifest.perform_adsr_loop_sustain[slot] = app.perform.perform_adsr_loop_sustain[slot];
        manifest.perform_adsr_loop_release[slot] = app.perform.perform_adsr_loop_release[slot];
        manifest.perform_adsr_loop_crossfade[slot] = app.perform.perform_adsr_loop_crossfade[slot];
        manifest.perform_adsr_loop_crossfade_shape[slot] = app.perform.perform_adsr_loop_crossfade_shape[slot];
        manifest.perform_adsr_env_a_x[slot] = app.perform.perform_adsr_env_a_x[slot];
        manifest.perform_adsr_env_d_x[slot] = app.perform.perform_adsr_env_d_x[slot];
        manifest.perform_adsr_env_r_x[slot] = app.perform.perform_adsr_env_r_x[slot];
        manifest.perform_adsr_env_s_level[slot] = app.perform.perform_adsr_env_s_level[slot];
        manifest.engine_gain_db[slot] = app.engine.engine_gain_db[slot];
        manifest.engine_drive_mode[slot] = app.engine.engine_drive_mode[slot];
        manifest.engine_filter_cutoff_hz[slot] = targets.engine_filter_cutoff_hz[slot];
        manifest.engine_filter_resonance[slot] = targets.engine_filter_resonance[slot];
    }

    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
    {
        manifest.engine_layer_master_level[slot]
            = ClampProjectFloat(targets.engine_layer_master_level[slot], 0.0f, 2.0f);
        if((manifest.sample_present_mask & static_cast<uint8_t>(1u << slot)) == 0u)
            manifest.engine_tune_semitones[slot] = ClampProjectTune(app.engine.engine_tune_semitones[slot]);
        manifest.perform_keyzone_lo_note[slot] = app.perform.perform_keyzone_lo_note[slot];
        manifest.perform_keyzone_hi_note[slot] = app.perform.perform_keyzone_hi_note[slot];
        ClampProjectKeyzoneRange(manifest.perform_keyzone_lo_note[slot],
                                 manifest.perform_keyzone_hi_note[slot]);
        manifest.perform_adsr_row[slot] = ClampProjectAdsrRow(app.perform.perform_adsr_row[slot]);
        manifest.engine_play_mode[slot] = ClampProjectPlayMode(app.engine.engine_play_mode[slot]);
        if(manifest.perform_adsr_loop_attack[slot] < 1u)
            manifest.perform_adsr_loop_attack[slot] = 1u;
        if(manifest.perform_adsr_loop_attack[slot] > 1000u)
            manifest.perform_adsr_loop_attack[slot] = 1000u;
        if(manifest.perform_adsr_loop_decay[slot] < 1u)
            manifest.perform_adsr_loop_decay[slot] = 1u;
        if(manifest.perform_adsr_loop_decay[slot] > 100u)
            manifest.perform_adsr_loop_decay[slot] = 100u;
        if(manifest.perform_adsr_loop_sustain[slot] > 100u)
            manifest.perform_adsr_loop_sustain[slot] = 100u;
        if(manifest.perform_adsr_loop_release[slot] < 1u)
            manifest.perform_adsr_loop_release[slot] = 1u;
        if(manifest.perform_adsr_loop_release[slot] > 1000u)
            manifest.perform_adsr_loop_release[slot] = 1000u;
        manifest.perform_adsr_loop_crossfade[slot]
            = ClampProjectFloat(manifest.perform_adsr_loop_crossfade[slot], 0.0f, 0.5f);
        manifest.perform_adsr_loop_crossfade_shape[slot]
            = ClampProjectFloat(manifest.perform_adsr_loop_crossfade_shape[slot], 0.0f, 1.0f);
        ClampProjectAdsrGraph(manifest.perform_adsr_env_a_x[slot],
                              manifest.perform_adsr_env_d_x[slot],
                              manifest.perform_adsr_env_r_x[slot],
                              manifest.perform_adsr_env_s_level[slot]);
        manifest.engine_gain_db[slot] = ClampProjectEngineGainDb(manifest.engine_gain_db[slot]);
        manifest.engine_drive_mode[slot] = ClampProjectDriveMode(manifest.engine_drive_mode[slot]);
        manifest.engine_filter_cutoff_hz[slot]
            = ClampProjectFilterCutoffHz(manifest.engine_filter_cutoff_hz[slot]);
        manifest.engine_filter_resonance[slot]
            = ClampProjectFloat(manifest.engine_filter_resonance[slot], 0.0f, 1.0f);
    }
}

static void CollectProjectGlobalState(ProjectManifestV10& manifest,
                                      const AppState& app,
                                      const PerformParamsTargets& targets)
{
    for(uint8_t i = 0; i < 4; ++i)
        manifest.fx_order[i] = targets.fx_order[i];
    SanitizeProjectFxOrder(manifest.fx_order);
    manifest.sat.sat_on = targets.sat_on ? 1u : 0u;
    manifest.sat.sat_mode = targets.sat_mode;
    manifest.sat.sat_mix = targets.sat_mix;
    manifest.sat.sat_drive = targets.sat_drive;
    manifest.sat.sat_bump = targets.sat_bump;
    manifest.sat.sat_bit_reso = targets.sat_bit_reso;
    manifest.sat.sat_bit_smpl = targets.sat_bit_smpl;
    ClampProjectSatState(manifest.sat);
    manifest.eq.eq_on = targets.eq_on ? 1u : 0u;
    manifest.eq.eq_mix = targets.eq_mix;
    manifest.eq.eq_center_norm = targets.eq_center_norm;
    manifest.eq.eq_tilt_db = targets.eq_tilt_db;
    manifest.eq.eq_q = targets.eq_q;
    ClampProjectEqState(manifest.eq);

    manifest.seq_running = app.seq_running ? 1 : 0;
    manifest.plock_apply_enabled = app.plock_apply_enabled ? 1 : 0;
    manifest.lfo_wave = app.lfo_wave.load(std::memory_order_relaxed);
    manifest.seq_bpm = app.seq_bpm;
    manifest.macro_ui = app.macro_ui;
    manifest.macro_sel = app.macro_sel.load(std::memory_order_relaxed) & 1u;
    for(size_t i = 0; i < kMaxModRoutes; ++i)
        manifest.mod_routes[i] = app.mod_routes_ui[i];
    manifest.mod_route_selected = app.mod_route_selected;
}

static bool WriteProjectManifestFile(uint8_t project_slot, const ProjectManifestV10& manifest)
{
    char tmp_path[kProjectPathMax];
    char prj_path[kProjectPathMax];
    const char* base = s_sd.fsi.GetSDPath();
    if(!MakeProjectSlotPath(tmp_path, sizeof(tmp_path), base, project_slot, "TMP")
       || !MakeProjectSlotPath(prj_path, sizeof(prj_path), base, project_slot, "AKPRJ"))
    {
        return false;
    }

    if(f_open(&s_sd.file, tmp_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return false;

    UINT bw = 0;
    const FRESULT wr = f_write(&s_sd.file, &manifest, sizeof(manifest), &bw);
    f_close(&s_sd.file);
    if(wr != FR_OK || bw != sizeof(manifest))
    {
        f_unlink(tmp_path);
        return false;
    }

    f_unlink(prj_path);
    if(f_rename(tmp_path, prj_path) != FR_OK)
    {
        f_unlink(tmp_path);
        return false;
    }

    return true;
}

bool SaveProject(AppState& app, const Params& params)
{
    const uint8_t project_slot = RequestedProjectSlot(app);
    SetProjectSlotStatus(app, project_slot, "SAVING");

    if(!EnsureSdMounted(app))
    {
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    ProjectManifestV10 manifest{};
    const PerformParamsTargets& targets = params.TargetsForUI();
    CollectProjectLayerState(manifest, app, targets);
    CollectProjectGlobalState(manifest, app, targets);

    if(manifest.sample_present_mask == 0u)
    {
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    if(!WriteProjectManifestFile(project_slot, manifest))
    {
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    SetProjectSlotStatus(app, project_slot, "SAVED");
    return true;
}

static bool ReadProjectManifestFromFile(ProjectManifestV10& manifest)
{
    const FSIZE_t manifest_size = f_size(&s_sd.file);
    UINT br = 0;
    FRESULT rd = FR_INT_ERR;
    if(manifest_size == sizeof(ProjectManifestV10))
    {
        rd = f_read(&s_sd.file, &manifest, sizeof(manifest), &br);
    }
    else if(manifest_size == sizeof(ProjectManifestV9))
    {
        ProjectManifestV9 legacy_v9{};
        rd = f_read(&s_sd.file, &legacy_v9, sizeof(legacy_v9), &br);
        if(rd == FR_OK && br == sizeof(legacy_v9) && ProjectManifestValid(legacy_v9))
            ProjectManifestUpgrade(manifest, legacy_v9);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV8))
    {
        ProjectManifestV8 legacy_v8{};
        rd = f_read(&s_sd.file, &legacy_v8, sizeof(legacy_v8), &br);
        if(rd == FR_OK && br == sizeof(legacy_v8) && ProjectManifestValid(legacy_v8))
        {
            ProjectManifestV9 legacy_v9{};
            ProjectManifestUpgrade(legacy_v9, legacy_v8);
            ProjectManifestUpgrade(manifest, legacy_v9);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV7))
    {
        ProjectManifestV7 legacy_v7{};
        rd = f_read(&s_sd.file, &legacy_v7, sizeof(legacy_v7), &br);
        if(rd == FR_OK && br == sizeof(legacy_v7) && ProjectManifestValid(legacy_v7))
        {
            ProjectManifestV8 legacy_v8{};
            ProjectManifestUpgrade(legacy_v8, legacy_v7);
            ProjectManifestV9 legacy_v9{};
            ProjectManifestUpgrade(legacy_v9, legacy_v8);
            ProjectManifestUpgrade(manifest, legacy_v9);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV6))
    {
        ProjectManifestV6 legacy_v6{};
        rd = f_read(&s_sd.file, &legacy_v6, sizeof(legacy_v6), &br);
        if(rd == FR_OK && br == sizeof(legacy_v6) && ProjectManifestValid(legacy_v6))
        {
            ProjectManifestV7 legacy_v7{};
            ProjectManifestUpgrade(legacy_v7, legacy_v6);
            ProjectManifestV8 legacy_v8{};
            ProjectManifestUpgrade(legacy_v8, legacy_v7);
            ProjectManifestV9 legacy_v9{};
            ProjectManifestUpgrade(legacy_v9, legacy_v8);
            ProjectManifestUpgrade(manifest, legacy_v9);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV5))
    {
        ProjectManifestV5 legacy_v5{};
        rd = f_read(&s_sd.file, &legacy_v5, sizeof(legacy_v5), &br);
        if(rd == FR_OK && br == sizeof(legacy_v5) && ProjectManifestValid(legacy_v5))
        {
            ProjectManifestV6 legacy_v6{};
            ProjectManifestUpgrade(legacy_v6, legacy_v5);
            ProjectManifestV7 legacy_v7{};
            ProjectManifestUpgrade(legacy_v7, legacy_v6);
            ProjectManifestV8 legacy_v8{};
            ProjectManifestUpgrade(legacy_v8, legacy_v7);
            ProjectManifestV9 legacy_v9{};
            ProjectManifestUpgrade(legacy_v9, legacy_v8);
            ProjectManifestUpgrade(manifest, legacy_v9);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV4))
    {
        ProjectManifestV4 legacy_v4{};
        rd = f_read(&s_sd.file, &legacy_v4, sizeof(legacy_v4), &br);
        if(rd == FR_OK && br == sizeof(legacy_v4) && ProjectManifestValid(legacy_v4))
        {
            ProjectManifestV5 legacy_v5{};
            ProjectManifestUpgrade(legacy_v5, legacy_v4);
            ProjectManifestV6 legacy_v6{};
            ProjectManifestUpgrade(legacy_v6, legacy_v5);
            ProjectManifestV7 legacy_v7{};
            ProjectManifestUpgrade(legacy_v7, legacy_v6);
            ProjectManifestV8 legacy_v8{};
            ProjectManifestUpgrade(legacy_v8, legacy_v7);
            ProjectManifestV9 legacy_v9{};
            ProjectManifestUpgrade(legacy_v9, legacy_v8);
            ProjectManifestUpgrade(manifest, legacy_v9);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV3))
    {
        ProjectManifestV3 legacy_v3{};
        rd = f_read(&s_sd.file, &legacy_v3, sizeof(legacy_v3), &br);
        if(rd == FR_OK && br == sizeof(legacy_v3) && ProjectManifestValid(legacy_v3))
        {
            ProjectManifestV4 legacy_v4{};
            ProjectManifestUpgrade(legacy_v4, legacy_v3);
            ProjectManifestV5 legacy_v5{};
            ProjectManifestUpgrade(legacy_v5, legacy_v4);
            ProjectManifestV6 legacy_v6{};
            ProjectManifestUpgrade(legacy_v6, legacy_v5);
            ProjectManifestV7 legacy_v7{};
            ProjectManifestUpgrade(legacy_v7, legacy_v6);
            ProjectManifestV8 legacy_v8{};
            ProjectManifestUpgrade(legacy_v8, legacy_v7);
            ProjectManifestV9 legacy_v9{};
            ProjectManifestUpgrade(legacy_v9, legacy_v8);
            ProjectManifestUpgrade(manifest, legacy_v9);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifest))
    {
        ProjectManifest legacy_v2{};
        rd = f_read(&s_sd.file, &legacy_v2, sizeof(legacy_v2), &br);
        if(rd == FR_OK && br == sizeof(legacy_v2) && ProjectManifestValid(legacy_v2))
        {
            ProjectManifestV3 legacy_v3{};
            ProjectManifestUpgrade(legacy_v3, legacy_v2);
            ProjectManifestV4 legacy_v4{};
            ProjectManifestUpgrade(legacy_v4, legacy_v3);
            ProjectManifestV5 legacy_v5{};
            ProjectManifestUpgrade(legacy_v5, legacy_v4);
            ProjectManifestV6 legacy_v6{};
            ProjectManifestUpgrade(legacy_v6, legacy_v5);
            ProjectManifestV7 legacy_v7{};
            ProjectManifestUpgrade(legacy_v7, legacy_v6);
            ProjectManifestV8 legacy_v8{};
            ProjectManifestUpgrade(legacy_v8, legacy_v7);
            ProjectManifestV9 legacy_v9{};
            ProjectManifestUpgrade(legacy_v9, legacy_v8);
            ProjectManifestUpgrade(manifest, legacy_v9);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV1))
    {
        ProjectManifestV1 legacy{};
        rd = f_read(&s_sd.file, &legacy, sizeof(legacy), &br);
        if(rd == FR_OK && br == sizeof(legacy) && ProjectManifestValid(legacy))
        {
            ProjectManifest legacy_v2{};
            ProjectManifestUpgrade(legacy_v2, legacy);
            ProjectManifestV3 legacy_v3{};
            ProjectManifestUpgrade(legacy_v3, legacy_v2);
            ProjectManifestV4 legacy_v4{};
            ProjectManifestUpgrade(legacy_v4, legacy_v3);
            ProjectManifestV5 legacy_v5{};
            ProjectManifestUpgrade(legacy_v5, legacy_v4);
            ProjectManifestV6 legacy_v6{};
            ProjectManifestUpgrade(legacy_v6, legacy_v5);
            ProjectManifestV7 legacy_v7{};
            ProjectManifestUpgrade(legacy_v7, legacy_v6);
            ProjectManifestV8 legacy_v8{};
            ProjectManifestUpgrade(legacy_v8, legacy_v7);
            ProjectManifestV9 legacy_v9{};
            ProjectManifestUpgrade(legacy_v9, legacy_v8);
            ProjectManifestUpgrade(manifest, legacy_v9);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    f_close(&s_sd.file);
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
        manifest.wav_path[slot][sizeof(manifest.wav_path[slot]) - 1] = '\0';

    return (manifest_size == sizeof(ProjectManifestV10) && rd == FR_OK && br == sizeof(manifest)
            && ProjectManifestValid(manifest))
           || (manifest_size == sizeof(ProjectManifestV9) && rd == FR_OK
               && br == sizeof(ProjectManifestV9))
           || (manifest_size == sizeof(ProjectManifestV8) && rd == FR_OK
               && br == sizeof(ProjectManifestV8))
           || (manifest_size == sizeof(ProjectManifestV7) && rd == FR_OK
               && br == sizeof(ProjectManifestV7))
           || (manifest_size == sizeof(ProjectManifestV6) && rd == FR_OK
               && br == sizeof(ProjectManifestV6))
           || (manifest_size == sizeof(ProjectManifestV5) && rd == FR_OK
               && br == sizeof(ProjectManifestV5))
           || (manifest_size == sizeof(ProjectManifestV4) && rd == FR_OK
               && br == sizeof(ProjectManifestV4))
           || (manifest_size == sizeof(ProjectManifestV3) && rd == FR_OK
               && br == sizeof(ProjectManifestV3))
           || (manifest_size == sizeof(ProjectManifest) && rd == FR_OK
               && br == sizeof(ProjectManifest))
           || (manifest_size == sizeof(ProjectManifestV1) && rd == FR_OK
               && br == sizeof(ProjectManifestV1));
}

static void ApplyProjectManifestGlobalState(AppState& app, const ProjectManifestV10& manifest)
{
    app.seq_running = (manifest.seq_running != 0);
    app.plock_apply_enabled = (manifest.plock_apply_enabled != 0);
    app.lfo_wave.store(manifest.lfo_wave, std::memory_order_release);
    app.seq_bpm = manifest.seq_bpm;

    app.macro_ui = manifest.macro_ui;
    app.macro_ui.selected = manifest.macro_ui.selected;
    Macros_Publish(app, app.macro_ui);

    for(size_t i = 0; i < kMaxModRoutes; ++i)
        app.mod_routes_ui[i] = manifest.mod_routes[i];
    app.mod_route_selected = manifest.mod_route_selected;
    ModMatrix_Publish(app.mod_matrix, app.mod_routes_ui);
}

static void ApplyProjectManifestLayerState(AppState& app, const ProjectManifestV10& manifest)
{
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        app.engine.engine_tune_semitones[slot] = ClampProjectTune(manifest.engine_tune_semitones[slot]);
        app.perform.perform_keyzone_lo_note[slot] = manifest.perform_keyzone_lo_note[slot];
        app.perform.perform_keyzone_hi_note[slot] = manifest.perform_keyzone_hi_note[slot];
        ClampProjectKeyzoneRange(app.perform.perform_keyzone_lo_note[slot],
                                 app.perform.perform_keyzone_hi_note[slot]);
        app.perform.perform_adsr_row[slot] = ClampProjectAdsrRow(manifest.perform_adsr_row[slot]);
        app.engine.engine_play_mode[slot] = ClampProjectPlayMode(manifest.engine_play_mode[slot]);
        app.perform.perform_adsr_loop_attack[slot] = manifest.perform_adsr_loop_attack[slot];
        if(app.perform.perform_adsr_loop_attack[slot] < 1u)
            app.perform.perform_adsr_loop_attack[slot] = 1u;
        if(app.perform.perform_adsr_loop_attack[slot] > 1000u)
            app.perform.perform_adsr_loop_attack[slot] = 1000u;
        app.perform.perform_adsr_loop_decay[slot] = manifest.perform_adsr_loop_decay[slot];
        if(app.perform.perform_adsr_loop_decay[slot] < 1u)
            app.perform.perform_adsr_loop_decay[slot] = 1u;
        if(app.perform.perform_adsr_loop_decay[slot] > 100u)
            app.perform.perform_adsr_loop_decay[slot] = 100u;
        app.perform.perform_adsr_loop_sustain[slot] = manifest.perform_adsr_loop_sustain[slot];
        if(app.perform.perform_adsr_loop_sustain[slot] > 100u)
            app.perform.perform_adsr_loop_sustain[slot] = 100u;
        app.perform.perform_adsr_loop_release[slot] = manifest.perform_adsr_loop_release[slot];
        if(app.perform.perform_adsr_loop_release[slot] < 1u)
            app.perform.perform_adsr_loop_release[slot] = 1u;
        if(app.perform.perform_adsr_loop_release[slot] > 1000u)
            app.perform.perform_adsr_loop_release[slot] = 1000u;
        app.perform.perform_adsr_loop_crossfade[slot]
            = ClampProjectFloat(manifest.perform_adsr_loop_crossfade[slot], 0.0f, 0.5f);
        app.perform.perform_adsr_loop_crossfade_shape[slot]
            = ClampProjectFloat(manifest.perform_adsr_loop_crossfade_shape[slot], 0.0f, 1.0f);
        app.perform.perform_adsr_env_a_x[slot] = manifest.perform_adsr_env_a_x[slot];
        app.perform.perform_adsr_env_d_x[slot] = manifest.perform_adsr_env_d_x[slot];
        app.perform.perform_adsr_env_r_x[slot] = manifest.perform_adsr_env_r_x[slot];
        app.perform.perform_adsr_env_s_level[slot] = manifest.perform_adsr_env_s_level[slot];
        ClampProjectAdsrGraph(app.perform.perform_adsr_env_a_x[slot],
                              app.perform.perform_adsr_env_d_x[slot],
                              app.perform.perform_adsr_env_r_x[slot],
                              app.perform.perform_adsr_env_s_level[slot]);
        app.engine.engine_gain_db[slot] = ClampProjectEngineGainDb(manifest.engine_gain_db[slot]);
        app.engine.engine_drive_mode[slot] = ClampProjectDriveMode(manifest.engine_drive_mode[slot]);
    }
}

static void SetupProjectRestoreState(AppState& app, const ProjectManifestV10& manifest)
{
    ClearProjectRestoreState(app);
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        if(!ProjectManifestHasLayer(manifest, slot))
            continue;

        const uint8_t bit = static_cast<uint8_t>(1u << slot);
        app.project.project_pending_edit[slot] = manifest.edit[slot];
        app.project.project_edit_pending_mask |= bit;
        s_sd.project_restore_pending_mask |= bit;
        std::snprintf(s_sd.project_restore_path[slot],
                      sizeof(s_sd.project_restore_path[slot]),
                      "%s",
                      manifest.wav_path[slot]);
        std::snprintf(app.engine.engine_sample_path[slot],
                      sizeof(app.engine.engine_sample_path[slot]),
                      "%s",
                      manifest.wav_path[slot]);
        WorkerExtractBaseName(manifest.wav_path[slot],
                              app.engine.engine_sample_name[slot],
                              sizeof(app.engine.engine_sample_name[slot]));
    }
}

bool LoadProject(AppState& app, Params& params)
{
    SdBrowserState& sd = app.sd;
    const uint8_t project_slot = RequestedProjectSlot(app);
    SetProjectSlotStatus(app, project_slot, "LOADING");

    if(!EnsureSdMounted(app))
    {
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    char prj_path[kProjectPathMax];
    const char* base = s_sd.fsi.GetSDPath();
    if(!MakeProjectSlotPath(prj_path, sizeof(prj_path), base, project_slot, "AKPRJ"))
    {
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    const FRESULT open_res = f_open(&s_sd.file, prj_path, FA_READ);
    if(open_res != FR_OK)
    {
        if(open_res == FR_NO_FILE || open_res == FR_NO_PATH)
            SetProjectSlotStatus(app, project_slot, "EMPTY");
        else
            SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    ProjectManifestV10 manifest{};
    if(!ReadProjectManifestFromFile(manifest))
    {
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    SanitizeProjectFxOrder(manifest.fx_order);
    ClampProjectSatState(manifest.sat);
    ClampProjectEqState(manifest.eq);

    ApplyProjectManifestGlobalState(app, manifest);
    ApplyProjectManifestLayerState(app, manifest);
    PublishProjectPerformParams(params,
                                app,
                                manifest.engine_layer_master_level,
                                manifest.fx_order,
                                &manifest.sat,
                                &manifest.eq,
                                manifest.engine_filter_cutoff_hz,
                                manifest.engine_filter_resonance);
    SyncProjectProcessVolumeUiState(app, manifest.engine_layer_master_level);
    SyncProjectProcessFxOrderUiState(app, manifest.fx_order);

    SetupProjectRestoreState(app, manifest);

    if(s_sd.project_restore_pending_mask == 0u)
    {
        sd.last_loaded_path[0] = '\0';
        return true;
    }

    if(!StartNextProjectRestoreLoad(app))
    {
        ClearProjectRestoreState(app);
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    return true;
}
