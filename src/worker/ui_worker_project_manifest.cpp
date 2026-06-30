#include "ui_worker_internal.h"

#include "app_state_project.h"
#include "mem_regions.h"

#include <cstdio>
#include <cstring>

// Manifest validity helpers (single magic/version check)
static bool ManifestMagicVersionOk(const char (&magic)[4], uint16_t version, uint16_t expected)
{
    return std::memcmp(magic, "AKPJ", 4) == 0 && version == expected;
}

static bool ProjectManifestValid(const ProjectManifestV1& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 1u);
}

static bool ProjectManifestValid(const ProjectManifest& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 2u);
}

static bool ProjectManifestValid(const ProjectManifestV3& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 3u);
}

static bool ProjectManifestValid(const ProjectManifestV4& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 4u);
}

static bool ProjectManifestValid(const ProjectManifestV5& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 5u);
}

static bool ProjectManifestValid(const ProjectManifestV6& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 6u);
}

static bool ProjectManifestValid(const ProjectManifestV7& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 7u);
}

static bool ProjectManifestValid(const ProjectManifestV8& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 8u);
}

static bool ProjectManifestValid(const ProjectManifestV9& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 9u);
}

static bool ProjectManifestValid(const ProjectManifestV10& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 10u);
}

static bool ProjectManifestValid(const ProjectManifestV11& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, kProjectManifestVersion);
}

static bool ProjectManifestValid(const ProjectManifestV23Legacy& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 23u);
}

static bool ProjectManifestValid(const ProjectManifestV24Legacy& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 24u);
}

static bool ProjectManifestValid(const ProjectManifestV22Legacy& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 22u);
}

static bool ProjectManifestValid(const ProjectManifestV21Legacy& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 21u);
}

static bool ProjectManifestValid(const ProjectManifestV20Legacy& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 20u);
}

static bool ProjectManifestValid(const ProjectManifestV19Legacy& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 19u);
}

// V18Legacy on-disk layout is shared by manifest versions 16, 17, and 18 —
// the field order/sizes didn't change across those revisions, only certain
// field semantics (handled by PrepareProjectLoadManifest after the upgrade
// copies src.version forward).
static bool ProjectManifestValid(const ProjectManifestV18Legacy& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 16u)
           || ManifestMagicVersionOk(m.magic, m.version, 17u)
           || ManifestMagicVersionOk(m.magic, m.version, 18u);
}

static bool ProjectManifestValid(const ProjectManifestV14Legacy& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 14u);
}

static bool ProjectManifestValid(const ProjectManifestV15Legacy& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 15u);
}

static bool ProjectManifestValid(const ProjectManifestV12Legacy& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 12u);
}

static bool ProjectManifestValid(const ProjectManifestV11Legacy& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 11u);
}

static bool ProjectManifestValid(const ProjectManifestV13Legacy& m)
{
    return ManifestMagicVersionOk(m.magic, m.version, 13u);
}

// Copies shared sequencer / modulation tail fields where struct layouts match.
template<typename TDst, typename TSrc>
static void CopySharedSeqModTail(TDst& dst, const TSrc& src)
{
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

// Manifest upgrade helpers
static void ProjectManifestUpgrade(ProjectManifest& dst, const ProjectManifestV1& src)
{
    dst = ProjectManifest{};
    std::snprintf(dst.wav_path[0], sizeof(dst.wav_path[0]), "%s", src.wav_path);
    dst.edit[0] = src.edit;
    dst.sample_present_mask = (src.wav_path[0] != '\0') ? 0x01u : 0u;
    CopySharedSeqModTail(dst, src);
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
    CopySharedSeqModTail(dst, src);
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
    CopySharedSeqModTail(dst, src);
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
        dst.engine_play_mode[slot] = (slot == 1u) ? 0u : 1u;
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
    CopySharedSeqModTail(dst, src);
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
    CopySharedSeqModTail(dst, src);
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
    CopySharedSeqModTail(dst, src);
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
    CopySharedSeqModTail(dst, src);
    dst.fx_order[0] = 0u;
    dst.fx_order[1] = 2u;
    dst.fx_order[2] = 3u;
    dst.fx_order[3] = 1u;
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
    CopySharedSeqModTail(dst, src);
}

static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV10& src)
{
    dst = ProjectManifestV11{};
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
    dst.eq = src.eq;
    // delay and reverb default-constructed (off, all params at defaults)
    CopySharedSeqModTail(dst, src);
}

static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV11Legacy& src)
{
    dst = ProjectManifestV11{};
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
    dst.eq = src.eq;
    dst.delay = src.delay;
    dst.reverb = src.reverb;
    CopySharedSeqModTail(dst, src);
}

static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV12Legacy& src)
{
    dst = ProjectManifestV11{};
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
        for(uint8_t row = 0; row < ProjectExpressState::kRowCount; ++row)
        {
            dst.express.target[slot][row] = src.express.target[slot][row];
            dst.express.min_value[slot][row] = src.express.min_value[slot][row];
            dst.express.max_value[slot][row] = src.express.max_value[slot][row];
        }
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.sat = src.sat;
    dst.eq = src.eq;
    dst.delay = src.delay;
    dst.reverb = src.reverb;
    CopySharedSeqModTail(dst, src);
}

static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV13Legacy& src)
{
    dst = ProjectManifestV11{};
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
        for(uint8_t row = 0; row < ProjectExpressState::kRowCount; ++row)
        {
            dst.express.target[slot][row] = src.express.target[slot][row];
            dst.express.min_value[slot][row] = src.express.min_value[slot][row];
            dst.express.max_value[slot][row] = src.express.max_value[slot][row];
        }
        dst.express.poly_porto_voice_limit[slot] = src.express.poly_porto_voice_limit[slot];
        dst.express.poly_porto_slide_ms[slot] = src.express.poly_porto_slide_ms[slot];
        dst.express.poly_porto_source_range_semitones[slot]
            = src.express.poly_porto_source_range_semitones[slot];
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.sat = src.sat;
    dst.eq = src.eq;
    dst.delay = src.delay;
    dst.reverb = src.reverb;
    dst.express_enabled = src.express_enabled ? 1u : 0u;
    CopySharedSeqModTail(dst, src);
}

static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV14Legacy& src)
{
    dst = ProjectManifestV11{};
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
        dst.engine_tune_cents[slot] = src.engine_tune_cents[slot];
        for(uint8_t row = 0; row < ProjectExpressState::kRowCount; ++row)
        {
            dst.express.target[slot][row] = src.express.target[slot][row];
            dst.express.min_value[slot][row] = src.express.min_value[slot][row];
            dst.express.max_value[slot][row] = src.express.max_value[slot][row];
        }
        dst.express.poly_porto_voice_limit[slot] = src.express.poly_porto_voice_limit[slot];
        dst.express.poly_porto_slide_ms[slot] = src.express.poly_porto_slide_ms[slot];
        dst.express.poly_porto_source_range_semitones[slot]
            = src.express.poly_porto_source_range_semitones[slot];
        dst.express.poly_porto_source_mode[slot] = src.express.poly_porto_source_mode[slot];
        dst.express.poly_porto_release_ms[slot] = src.express.poly_porto_release_ms[slot];
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.sat = src.sat;
    dst.eq = src.eq;
    dst.delay = src.delay;
    dst.reverb = src.reverb;
    dst.express_enabled = src.express_enabled ? 1u : 0u;
    CopySharedSeqModTail(dst, src);
}

static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV15Legacy& src)
{
    dst = ProjectManifestV11{};
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
        dst.engine_tune_cents[slot] = src.engine_tune_cents[slot];
        for(uint8_t row = 0; row < ProjectExpressState::kRowCount; ++row)
        {
            dst.express.target[slot][row] = src.express.target[slot][row];
            dst.express.min_value[slot][row] = src.express.min_value[slot][row];
            dst.express.max_value[slot][row] = src.express.max_value[slot][row];
        }
        dst.express.poly_porto_voice_limit[slot] = src.express.poly_porto_voice_limit[slot];
        dst.express.poly_porto_slide_ms[slot] = src.express.poly_porto_slide_ms[slot];
        dst.express.poly_porto_source_range_semitones[slot]
            = src.express.poly_porto_source_range_semitones[slot];
        dst.express.poly_porto_source_mode[slot] = src.express.poly_porto_source_mode[slot];
        dst.express.poly_porto_release_ms[slot] = src.express.poly_porto_release_ms[slot];
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.sat = src.sat;
    dst.eq = src.eq;
    dst.delay = src.delay;
    dst.reverb = src.reverb;
    dst.express_enabled = src.express_enabled ? 1u : 0u;
    std::snprintf(dst.project_name, sizeof(dst.project_name), "%s", src.project_name);
    CopySharedSeqModTail(dst, src);
}

// V18Legacy → V11 (current). Pure field copy plus default master_level = 1.0f.
// Preserves src.version so PrepareProjectLoadManifest can still patch
// version-16 and version-17 field semantics; the final version bump to
// kProjectManifestVersion happens there.
// V19Legacy → V11 (current). Field-by-field copy (velmod tail was appended at
// v20 and keeps its default gate/off values on dst). Preserves src.version so
// PrepareProjectLoadManifest can patch version-specific semantics before the
// final bump to kProjectManifestVersion.
// v20 → v21: layout is byte-identical except the appended velmod_source[2].
// Copy every v20 field; velmod_source keeps its default (0 = >vel).
// v21 → v22: layout is byte-identical except the appended sat_tone/sat_bias.
// Copy every v21 field (including velmod_source); sat_tone/sat_bias keep their
// defaults (neutral tone, no bias).
// v22 → v23: layout is byte-identical except the appended perform_keytrack_tilt.
// Copy every v22 field (including sat_tone/sat_bias); perform_keytrack_tilt keeps
// its default (0 = flat).
static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV24Legacy& src)
{
    // v24 -> v25: identical layout except the top-level low/high EQ shelf fields
    // appended at v25 (left at their defaults below). Everything else copies 1:1.
    dst = ProjectManifestV11{};
    dst.version = src.version;
    dst.sample_present_mask = src.sample_present_mask;
    dst.adsr_curve_flags = src.adsr_curve_flags;
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
        dst.engine_tune_cents[slot] = src.engine_tune_cents[slot];
        dst.engine_filter_mode[slot] = src.engine_filter_mode[slot];
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.sat = src.sat;
    dst.eq = src.eq;
    dst.delay = src.delay;
    dst.reverb = src.reverb;
    dst.express = src.express;
    dst.express_enabled = src.express_enabled ? 1u : 0u;
    std::snprintf(dst.project_name, sizeof(dst.project_name), "%s", src.project_name);
    dst.project_style = src.project_style;
    CopySharedSeqModTail(dst, src);
    dst.master_level = src.master_level;
    for(uint8_t lane = 0; lane < 2u; ++lane)
    {
        dst.velmod_target[lane] = src.velmod_target[lane];
        dst.velmod_amount[lane] = src.velmod_amount[lane];
        dst.velmod_threshold[lane] = src.velmod_threshold[lane];
        dst.velmod_shape[lane] = src.velmod_shape[lane];
        dst.velmod_source[lane] = src.velmod_source[lane];
    }
    dst.velmod_threshold_linked = src.velmod_threshold_linked;
    dst.perform_keyzone_is_split = src.perform_keyzone_is_split;
    dst.sat_tone = src.sat_tone;
    dst.sat_bias = src.sat_bias;
    dst.perform_keytrack_tilt = src.perform_keytrack_tilt;
    dst.perform_keytrack_amount_db = src.perform_keytrack_amount_db;
    dst.perform_keytrack_mid_note = src.perform_keytrack_mid_note;
    // eq lo/hi fields keep their (flat) defaults.
}

static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV23Legacy& src)
{
    // v23 -> v24: identical layout except perform_adsr_loop_decay widened
    // uint8_t -> uint16_t (assignment below promotes it). All other fields copy
    // straight across, including the v23 keytrack additions.
    dst = ProjectManifestV11{};
    dst.version = src.version;
    dst.sample_present_mask = src.sample_present_mask;
    dst.adsr_curve_flags = src.adsr_curve_flags;
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
        dst.engine_tune_cents[slot] = src.engine_tune_cents[slot];
        dst.engine_filter_mode[slot] = src.engine_filter_mode[slot];
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.sat = src.sat;
    dst.eq = src.eq;
    dst.delay = src.delay;
    dst.reverb = src.reverb;
    dst.express = src.express;
    dst.express_enabled = src.express_enabled ? 1u : 0u;
    std::snprintf(dst.project_name, sizeof(dst.project_name), "%s", src.project_name);
    dst.project_style = src.project_style;
    CopySharedSeqModTail(dst, src);
    dst.master_level = src.master_level;
    for(uint8_t lane = 0; lane < 2u; ++lane)
    {
        dst.velmod_target[lane] = src.velmod_target[lane];
        dst.velmod_amount[lane] = src.velmod_amount[lane];
        dst.velmod_threshold[lane] = src.velmod_threshold[lane];
        dst.velmod_shape[lane] = src.velmod_shape[lane];
        dst.velmod_source[lane] = src.velmod_source[lane];
    }
    dst.velmod_threshold_linked = src.velmod_threshold_linked;
    dst.perform_keyzone_is_split = src.perform_keyzone_is_split;
    dst.sat_tone = src.sat_tone;
    dst.sat_bias = src.sat_bias;
    dst.perform_keytrack_tilt = src.perform_keytrack_tilt;
    dst.perform_keytrack_amount_db = src.perform_keytrack_amount_db;
    dst.perform_keytrack_mid_note = src.perform_keytrack_mid_note;
}

static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV22Legacy& src)
{
    dst = ProjectManifestV11{};
    dst.version = src.version;
    dst.sample_present_mask = src.sample_present_mask;
    dst.adsr_curve_flags = src.adsr_curve_flags;
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
        dst.engine_tune_cents[slot] = src.engine_tune_cents[slot];
        dst.engine_filter_mode[slot] = src.engine_filter_mode[slot];
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.sat = src.sat;
    dst.eq = src.eq;
    dst.delay = src.delay;
    dst.reverb = src.reverb;
    dst.express = src.express;
    dst.express_enabled = src.express_enabled ? 1u : 0u;
    std::snprintf(dst.project_name, sizeof(dst.project_name), "%s", src.project_name);
    dst.project_style = src.project_style;
    CopySharedSeqModTail(dst, src);
    dst.master_level = src.master_level;
    for(uint8_t lane = 0; lane < 2u; ++lane)
    {
        dst.velmod_target[lane] = src.velmod_target[lane];
        dst.velmod_amount[lane] = src.velmod_amount[lane];
        dst.velmod_threshold[lane] = src.velmod_threshold[lane];
        dst.velmod_shape[lane] = src.velmod_shape[lane];
        dst.velmod_source[lane] = src.velmod_source[lane];
    }
    dst.velmod_threshold_linked = src.velmod_threshold_linked;
    dst.perform_keyzone_is_split = src.perform_keyzone_is_split;
    dst.sat_tone = src.sat_tone;
    dst.sat_bias = src.sat_bias;
}

static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV21Legacy& src)
{
    dst = ProjectManifestV11{};
    dst.version = src.version;
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
        dst.engine_tune_cents[slot] = src.engine_tune_cents[slot];
        dst.engine_filter_mode[slot] = src.engine_filter_mode[slot];
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.sat = src.sat;
    dst.eq = src.eq;
    dst.delay = src.delay;
    dst.reverb = src.reverb;
    dst.express = src.express;
    dst.express_enabled = src.express_enabled ? 1u : 0u;
    std::snprintf(dst.project_name, sizeof(dst.project_name), "%s", src.project_name);
    dst.project_style = src.project_style;
    CopySharedSeqModTail(dst, src);
    dst.master_level = src.master_level;
    for(uint8_t lane = 0; lane < 2u; ++lane)
    {
        dst.velmod_target[lane] = src.velmod_target[lane];
        dst.velmod_amount[lane] = src.velmod_amount[lane];
        dst.velmod_threshold[lane] = src.velmod_threshold[lane];
        dst.velmod_shape[lane] = src.velmod_shape[lane];
        dst.velmod_source[lane] = src.velmod_source[lane];
    }
    dst.velmod_threshold_linked = src.velmod_threshold_linked;
    dst.perform_keyzone_is_split = src.perform_keyzone_is_split;
}

static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV20Legacy& src)
{
    dst = ProjectManifestV11{};
    dst.version = src.version;
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
        dst.engine_tune_cents[slot] = src.engine_tune_cents[slot];
        dst.engine_filter_mode[slot] = src.engine_filter_mode[slot];
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.sat = src.sat;
    dst.eq = src.eq;
    dst.delay = src.delay;
    dst.reverb = src.reverb;
    dst.express = src.express;
    dst.express_enabled = src.express_enabled ? 1u : 0u;
    std::snprintf(dst.project_name, sizeof(dst.project_name), "%s", src.project_name);
    dst.project_style = src.project_style;
    CopySharedSeqModTail(dst, src);
    dst.master_level = src.master_level;
    for(uint8_t lane = 0; lane < 2u; ++lane)
    {
        dst.velmod_target[lane] = src.velmod_target[lane];
        dst.velmod_amount[lane] = src.velmod_amount[lane];
        dst.velmod_threshold[lane] = src.velmod_threshold[lane];
        dst.velmod_shape[lane] = src.velmod_shape[lane];
    }
    dst.velmod_threshold_linked = src.velmod_threshold_linked;
    dst.perform_keyzone_is_split = src.perform_keyzone_is_split;
}

static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV19Legacy& src)
{
    dst = ProjectManifestV11{};
    dst.version = src.version;
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
        dst.engine_tune_cents[slot] = src.engine_tune_cents[slot];
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.sat = src.sat;
    dst.eq = src.eq;
    dst.delay = src.delay;
    dst.reverb = src.reverb;
    dst.express = src.express;
    dst.express_enabled = src.express_enabled ? 1u : 0u;
    std::snprintf(dst.project_name, sizeof(dst.project_name), "%s", src.project_name);
    dst.project_style = src.project_style;
    CopySharedSeqModTail(dst, src);
    dst.master_level = src.master_level;
}

static void ProjectManifestUpgrade(ProjectManifestV11& dst, const ProjectManifestV18Legacy& src)
{
    dst = ProjectManifestV11{};
    dst.version = src.version;
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
        dst.engine_tune_cents[slot] = src.engine_tune_cents[slot];
    }
    for(size_t i = 0; i < 4; ++i)
        dst.fx_order[i] = src.fx_order[i];
    dst.sat = src.sat;
    dst.eq = src.eq;
    dst.delay = src.delay;
    dst.reverb = src.reverb;
    dst.express = src.express;
    dst.express_enabled = src.express_enabled ? 1u : 0u;
    std::snprintf(dst.project_name, sizeof(dst.project_name), "%s", src.project_name);
    dst.project_style = src.project_style;
    CopySharedSeqModTail(dst, src);
    dst.master_level = 1.0f; // unity for upgraded saves
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
    CopySharedSeqModTail(dst, src);
}

// SDMMC DMA on this STM32H7 cannot reach DTCM, where these manifest structs live
// as main-loop-stack locals. f_read straight into them silently transfers 0 bytes
// (or garbage), so a valid file "fails validation" and save/load report ERROR.
// Bounce every manifest read through this SDRAM-resident scratch, then memcpy the
// bytes out (CPU copy to the DTCM destination is fine — only DMA can't reach it).
// Same constraint/fix the baker uses (see bk_file_reader.cpp).
ADSR2_SECTION(".sdram_bss") static uint8_t s_manifest_rd_io[4096];
static_assert(sizeof(ProjectManifestV11) <= sizeof(s_manifest_rd_io),
              "manifest read scratch too small");

// Drop-in replacement for ReadSdDmaSafe(&s_sd.file, dst, n, br) whose DMA target is the
// SDRAM scratch rather than the caller's (DTCM) buffer.
static FRESULT ReadSdDmaSafe(FIL* fp, void* dst, UINT n, UINT* br)
{
    if(br)
        *br = 0;
    if(n > sizeof(s_manifest_rd_io))
        return FR_INVALID_OBJECT;
    UINT got = 0;
    const FRESULT res = f_read(fp, s_manifest_rd_io, n, &got);
    if(br)
        *br = got;
    if(res == FR_OK && got > 0)
        std::memcpy(dst, s_manifest_rd_io, got);
    return res;
}

bool ReadProjectManifestFromFile(ProjectManifestV11& manifest)
{
    const FSIZE_t manifest_size = f_size(&s_sd.file);
    UINT br = 0;
    FRESULT rd = FR_INT_ERR;
    if(manifest_size == sizeof(ProjectManifestV11))
    {
        rd = ReadSdDmaSafe(&s_sd.file, &manifest, sizeof(manifest), &br);
    }
    else if(manifest_size == sizeof(ProjectManifestV24Legacy))
    {
        ProjectManifestV24Legacy legacy_v24{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v24, sizeof(legacy_v24), &br);
        if(rd == FR_OK && br == sizeof(legacy_v24) && ProjectManifestValid(legacy_v24))
            ProjectManifestUpgrade(manifest, legacy_v24);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV23Legacy))
    {
        ProjectManifestV23Legacy legacy_v23{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v23, sizeof(legacy_v23), &br);
        if(rd == FR_OK && br == sizeof(legacy_v23) && ProjectManifestValid(legacy_v23))
            ProjectManifestUpgrade(manifest, legacy_v23);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV22Legacy))
    {
        ProjectManifestV22Legacy legacy_v22{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v22, sizeof(legacy_v22), &br);
        if(rd == FR_OK && br == sizeof(legacy_v22) && ProjectManifestValid(legacy_v22))
            ProjectManifestUpgrade(manifest, legacy_v22);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV21Legacy))
    {
        ProjectManifestV21Legacy legacy_v21{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v21, sizeof(legacy_v21), &br);
        if(rd == FR_OK && br == sizeof(legacy_v21) && ProjectManifestValid(legacy_v21))
            ProjectManifestUpgrade(manifest, legacy_v21);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV20Legacy))
    {
        ProjectManifestV20Legacy legacy_v20{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v20, sizeof(legacy_v20), &br);
        if(rd == FR_OK && br == sizeof(legacy_v20) && ProjectManifestValid(legacy_v20))
            ProjectManifestUpgrade(manifest, legacy_v20);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV19Legacy))
    {
        ProjectManifestV19Legacy legacy_v19{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v19, sizeof(legacy_v19), &br);
        if(rd == FR_OK && br == sizeof(legacy_v19) && ProjectManifestValid(legacy_v19))
            ProjectManifestUpgrade(manifest, legacy_v19);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV18Legacy))
    {
        ProjectManifestV18Legacy legacy_v18{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v18, sizeof(legacy_v18), &br);
        if(rd == FR_OK && br == sizeof(legacy_v18) && ProjectManifestValid(legacy_v18))
            ProjectManifestUpgrade(manifest, legacy_v18);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV15Legacy))
    {
        ProjectManifestV15Legacy legacy_v15{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v15, sizeof(legacy_v15), &br);
        if(rd == FR_OK && br == sizeof(legacy_v15) && ProjectManifestValid(legacy_v15))
            ProjectManifestUpgrade(manifest, legacy_v15);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV14Legacy))
    {
        ProjectManifestV14Legacy legacy_v14{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v14, sizeof(legacy_v14), &br);
        if(rd == FR_OK && br == sizeof(legacy_v14) && ProjectManifestValid(legacy_v14))
            ProjectManifestUpgrade(manifest, legacy_v14);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV13Legacy))
    {
        ProjectManifestV13Legacy legacy_v13{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v13, sizeof(legacy_v13), &br);
        if(rd == FR_OK && br == sizeof(legacy_v13) && ProjectManifestValid(legacy_v13))
            ProjectManifestUpgrade(manifest, legacy_v13);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV12Legacy))
    {
        ProjectManifestV12Legacy legacy_v12{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v12, sizeof(legacy_v12), &br);
        if(rd == FR_OK && br == sizeof(legacy_v12) && ProjectManifestValid(legacy_v12))
            ProjectManifestUpgrade(manifest, legacy_v12);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV11Legacy))
    {
        ProjectManifestV11Legacy legacy_v11{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v11, sizeof(legacy_v11), &br);
        if(rd == FR_OK && br == sizeof(legacy_v11) && ProjectManifestValid(legacy_v11))
            ProjectManifestUpgrade(manifest, legacy_v11);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV10))
    {
        ProjectManifestV10 legacy_v10{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v10, sizeof(legacy_v10), &br);
        if(rd == FR_OK && br == sizeof(legacy_v10) && ProjectManifestValid(legacy_v10))
            ProjectManifestUpgrade(manifest, legacy_v10);
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV9))
    {
        ProjectManifestV9 legacy_v9{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v9, sizeof(legacy_v9), &br);
        if(rd == FR_OK && br == sizeof(legacy_v9) && ProjectManifestValid(legacy_v9))
        {
            ProjectManifestV10 legacy_v10{};
            ProjectManifestUpgrade(legacy_v10, legacy_v9);
            ProjectManifestUpgrade(manifest, legacy_v10);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV8))
    {
        ProjectManifestV8 legacy_v8{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v8, sizeof(legacy_v8), &br);
        if(rd == FR_OK && br == sizeof(legacy_v8) && ProjectManifestValid(legacy_v8))
        {
            ProjectManifestV9 legacy_v9{};
            ProjectManifestUpgrade(legacy_v9, legacy_v8);
            ProjectManifestV10 legacy_v10{};
            ProjectManifestUpgrade(legacy_v10, legacy_v9);
            ProjectManifestUpgrade(manifest, legacy_v10);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV7))
    {
        ProjectManifestV7 legacy_v7{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v7, sizeof(legacy_v7), &br);
        if(rd == FR_OK && br == sizeof(legacy_v7) && ProjectManifestValid(legacy_v7))
        {
            ProjectManifestV8 legacy_v8{};
            ProjectManifestUpgrade(legacy_v8, legacy_v7);
            ProjectManifestV9 legacy_v9{};
            ProjectManifestUpgrade(legacy_v9, legacy_v8);
            ProjectManifestV10 legacy_v10{};
            ProjectManifestUpgrade(legacy_v10, legacy_v9);
            ProjectManifestUpgrade(manifest, legacy_v10);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV6))
    {
        ProjectManifestV6 legacy_v6{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v6, sizeof(legacy_v6), &br);
        if(rd == FR_OK && br == sizeof(legacy_v6) && ProjectManifestValid(legacy_v6))
        {
            ProjectManifestV7 legacy_v7{};
            ProjectManifestUpgrade(legacy_v7, legacy_v6);
            ProjectManifestV8 legacy_v8{};
            ProjectManifestUpgrade(legacy_v8, legacy_v7);
            ProjectManifestV9 legacy_v9{};
            ProjectManifestUpgrade(legacy_v9, legacy_v8);
            ProjectManifestV10 legacy_v10{};
            ProjectManifestUpgrade(legacy_v10, legacy_v9);
            ProjectManifestUpgrade(manifest, legacy_v10);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV5))
    {
        ProjectManifestV5 legacy_v5{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v5, sizeof(legacy_v5), &br);
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
            ProjectManifestV10 legacy_v10{};
            ProjectManifestUpgrade(legacy_v10, legacy_v9);
            ProjectManifestUpgrade(manifest, legacy_v10);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV4))
    {
        ProjectManifestV4 legacy_v4{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v4, sizeof(legacy_v4), &br);
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
            ProjectManifestV10 legacy_v10{};
            ProjectManifestUpgrade(legacy_v10, legacy_v9);
            ProjectManifestUpgrade(manifest, legacy_v10);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV3))
    {
        ProjectManifestV3 legacy_v3{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v3, sizeof(legacy_v3), &br);
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
            ProjectManifestV10 legacy_v10{};
            ProjectManifestUpgrade(legacy_v10, legacy_v9);
            ProjectManifestUpgrade(manifest, legacy_v10);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifest))
    {
        ProjectManifest legacy_v2{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy_v2, sizeof(legacy_v2), &br);
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
            ProjectManifestV10 legacy_v10{};
            ProjectManifestUpgrade(legacy_v10, legacy_v9);
            ProjectManifestUpgrade(manifest, legacy_v10);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    else if(manifest_size == sizeof(ProjectManifestV1))
    {
        ProjectManifestV1 legacy{};
        rd = ReadSdDmaSafe(&s_sd.file, &legacy, sizeof(legacy), &br);
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
            ProjectManifestV10 legacy_v10{};
            ProjectManifestUpgrade(legacy_v10, legacy_v9);
            ProjectManifestUpgrade(manifest, legacy_v10);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    f_close(&s_sd.file);
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
        manifest.wav_path[slot][sizeof(manifest.wav_path[slot]) - 1] = '\0';
    manifest.project_name[sizeof(manifest.project_name) - 1] = '\0';
    manifest.project_style = ProjectStyleToStored(ProjectStyleFromStored(manifest.project_style));

    return (manifest_size == sizeof(ProjectManifestV11) && rd == FR_OK && br == sizeof(manifest)
            && ProjectManifestValid(manifest))
           || (manifest_size == sizeof(ProjectManifestV24Legacy) && rd == FR_OK
               && br == sizeof(ProjectManifestV24Legacy))
           || (manifest_size == sizeof(ProjectManifestV23Legacy) && rd == FR_OK
               && br == sizeof(ProjectManifestV23Legacy))
           || (manifest_size == sizeof(ProjectManifestV22Legacy) && rd == FR_OK
               && br == sizeof(ProjectManifestV22Legacy))
           || (manifest_size == sizeof(ProjectManifestV21Legacy) && rd == FR_OK
               && br == sizeof(ProjectManifestV21Legacy))
           || (manifest_size == sizeof(ProjectManifestV18Legacy) && rd == FR_OK
               && br == sizeof(ProjectManifestV18Legacy))
           || (manifest_size == sizeof(ProjectManifestV15Legacy) && rd == FR_OK
               && br == sizeof(ProjectManifestV15Legacy))
           || (manifest_size == sizeof(ProjectManifestV14Legacy) && rd == FR_OK
               && br == sizeof(ProjectManifestV14Legacy))
           || (manifest_size == sizeof(ProjectManifestV13Legacy) && rd == FR_OK
               && br == sizeof(ProjectManifestV13Legacy))
           || (manifest_size == sizeof(ProjectManifestV12Legacy) && rd == FR_OK
               && br == sizeof(ProjectManifestV12Legacy))
           || (manifest_size == sizeof(ProjectManifestV11Legacy) && rd == FR_OK
               && br == sizeof(ProjectManifestV11Legacy))
           || (manifest_size == sizeof(ProjectManifestV10) && rd == FR_OK
               && br == sizeof(ProjectManifestV10))
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
