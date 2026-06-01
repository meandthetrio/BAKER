#include "ui_worker_project_internal.h"

#include "ui_worker_internal.h"

#include "app_state_engine.h"
#include "app_state_project.h"
#include "app_state_shared.h"
#include "app_state_ui.h"
#include "app_state_worker.h"
#include "macros.h"
#include "mod_matrix.h"
#include "params.h"
#include "express_state.h"
#include "tilt_eq.h"

#include "fatfs.h"
#include "ff.h"

#include <cstdio>
#include <cstring>

extern SdWorkerState s_sd;

static void PublishProjectPerformParams(Params& params,
                                        const AppEngineState& engine,
                                        const float* process_layer_master_level,
                                        const uint8_t* process_fx_order,
                                        const ProjectSatState* process_sat_state,
                                        const ProjectEqState* process_eq_state,
                                        const ProjectDelayState* process_delay_state,
                                        const ProjectReverbState* process_reverb_state,
                                        const float* emphasis_cutoff_hz,
                                        const float* emphasis_resonance)
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
    if(process_delay_state)
    {
        t.delay_on = (process_delay_state->delay_on != 0u);
        t.delay_mix = ClampProjectFloat(process_delay_state->delay_mix, 0.0f, 1.0f);
        t.delay_time_l = ClampProjectFloat(process_delay_state->delay_time_l, 0.0f, 1.0f);
        t.delay_time_r = ClampProjectFloat(process_delay_state->delay_time_r, 0.0f, 1.0f);
        t.delay_feedback = ClampProjectFloat(process_delay_state->delay_feedback, 0.0f, 1.0f);
    }
    if(process_reverb_state)
    {
        t.reverb_on = (process_reverb_state->reverb_on != 0u);
        t.reverb_fader_mode = (process_reverb_state->reverb_fader_mode == 0u)
                                  ? kReverbFaderModeSend
                                  : kReverbFaderModeMix;
        t.reverb_mix = ClampProjectFloat(process_reverb_state->reverb_mix, 0.0f, 1.0f);
        t.reverb_pre = ClampProjectFloat(process_reverb_state->reverb_pre, 0.0f, 1.0f);
        t.reverb_damp = ClampProjectFloat(process_reverb_state->reverb_damp, 0.0f, 1.0f);
        t.reverb_decay = ClampProjectFloat(process_reverb_state->reverb_decay, 0.0f, 1.0f);
        t.reverb_mod = ClampProjectFloat(process_reverb_state->reverb_mod, 0.0f, 1.0f);
    }
    for(uint8_t layer = 0; layer < kProjectSampleLayerCount; ++layer)
    {
        t.engine_tune_semitones[layer] = static_cast<float>(engine.layer.engine_tune_semitones[layer])
                                         + (static_cast<float>(engine.layer.engine_tune_cents[layer]) * 0.01f);
        if(process_layer_master_level)
        {
            t.engine_layer_master_level[layer]
                = ClampProjectFloat(process_layer_master_level[layer], 0.0f, 2.0f);
        }
        t.engine_gain_db[layer] = static_cast<float>(engine.layer.engine_gain_db[layer]);
        t.engine_drive_mode[layer] = ClampProjectDriveMode(engine.layer.engine_drive_mode[layer]);
        if(emphasis_cutoff_hz)
            t.engine_filter_cutoff_hz[layer] = ClampProjectFilterCutoffHz(emphasis_cutoff_hz[layer]);
        if(emphasis_resonance)
            t.engine_filter_resonance[layer] = ClampProjectFloat(emphasis_resonance[layer], 0.0f, 1.0f);
        t.perform_keyzone_lo_note[layer] = engine.keyzone.perform_keyzone_lo_note[layer];
        t.perform_keyzone_hi_note[layer] = engine.keyzone.perform_keyzone_hi_note[layer];
        for(uint8_t row = 0; row < kExpressRowCount; ++row)
        {
            t.express_target[layer][row] = engine.express.target[layer][row];
            t.express_min_value[layer][row] = engine.express.min_value[layer][row];
            t.express_max_value[layer][row] = engine.express.max_value[layer][row];
            ExpressClampRow(t.express_target[layer][row],
                            t.express_min_value[layer][row],
                            t.express_max_value[layer][row]);
        }
        t.express_poly_porto_voice_limit[layer] = engine.express.poly_porto_voice_limit[layer];
        t.express_poly_porto_slide_ms[layer] = engine.express.poly_porto_slide_ms[layer];
        t.express_poly_porto_source_range_semitones[layer]
            = engine.express.poly_porto_source_range_semitones[layer];
        t.express_poly_porto_source_mode[layer] = engine.express.poly_porto_source_mode[layer];
        t.express_poly_porto_release_ms[layer] = engine.express.poly_porto_release_ms[layer];
        ExpressClampPolyPortoConfig(t.express_poly_porto_voice_limit[layer],
                                    t.express_poly_porto_slide_ms[layer],
                                    t.express_poly_porto_source_range_semitones[layer],
                                    t.express_poly_porto_source_mode[layer],
                                    t.express_poly_porto_release_ms[layer]);
        t.engine_loop_mode[layer] = (engine.layer.engine_play_mode[layer] != 0);
        t.engine_loop_attack_ms[layer] = static_cast<float>(engine.adsr.perform_adsr_loop_attack[layer]);
        t.engine_loop_decay_ms[layer] = static_cast<float>(engine.adsr.perform_adsr_loop_decay[layer]);
        t.engine_loop_sustain_level[layer]
            = static_cast<float>(engine.adsr.perform_adsr_loop_sustain[layer]) * 0.01f;
        t.engine_loop_release_ms[layer] = static_cast<float>(engine.adsr.perform_adsr_loop_release[layer]);
        t.engine_loop_crossfade_amount[layer] = engine.adsr.perform_adsr_loop_crossfade[layer];
        t.engine_loop_crossfade_shape[layer] = engine.adsr.perform_adsr_loop_crossfade_shape[layer];
    }
    params.PublishTargets();
}

static void SyncProjectProcessVolumeUiState(AppEngineState& engine, const float* process_layer_master_level)
{
    if(!process_layer_master_level)
        return;

    for(uint8_t layer = 0; layer < kProjectSampleLayerCount; ++layer)
    {
        const float level = ClampProjectFloat(process_layer_master_level[layer], 0.0f, 2.0f);
        engine.process.perform_process_vol_muted[layer] = false;
        engine.process.perform_process_vol_unmuted_level[layer] = level;
        engine.process.perform_process_vol_pct[layer] = static_cast<uint16_t>(level * 100.0f + 0.5f);
    }
}

static void SyncProjectProcessFxOrderUiState(AppEngineState& engine, const uint8_t* process_fx_order)
{
    if(!process_fx_order)
        return;

    for(uint8_t i = 0; i < 4; ++i)
        engine.process.perform_process_fx_order[i] = process_fx_order[i];
    SanitizeProjectFxOrder(engine.process.perform_process_fx_order);
}

static void ApplyProjectExpressRow(AppEngineState& engine,
                                   const ProjectManifestV11& manifest,
                                   uint8_t layer,
                                   uint8_t row)
{
    uint8_t target = ExpressClampTarget(manifest.express.target[layer][row]);
    uint16_t min_value = manifest.express.min_value[layer][row];
    uint16_t max_value = manifest.express.max_value[layer][row];
    ExpressClampRow(target, min_value, max_value);
    engine.express.target[layer][row] = target;
    engine.express.min_value[layer][row] = min_value;
    engine.express.max_value[layer][row] = max_value;
}

static void ApplyProjectExpressPolyPorto(AppEngineState& engine,
                                         const ProjectManifestV11& manifest,
                                         uint8_t layer)
{
    engine.express.poly_porto_voice_limit[layer] = manifest.express.poly_porto_voice_limit[layer];
    engine.express.poly_porto_slide_ms[layer] = manifest.express.poly_porto_slide_ms[layer];
    engine.express.poly_porto_source_range_semitones[layer]
        = manifest.express.poly_porto_source_range_semitones[layer];
    engine.express.poly_porto_source_mode[layer] = manifest.express.poly_porto_source_mode[layer];
    engine.express.poly_porto_release_ms[layer] = manifest.express.poly_porto_release_ms[layer];
    ExpressClampPolyPortoConfig(engine.express.poly_porto_voice_limit[layer],
                                engine.express.poly_porto_slide_ms[layer],
                                engine.express.poly_porto_source_range_semitones[layer],
                                engine.express.poly_porto_source_mode[layer],
                                engine.express.poly_porto_release_ms[layer]);
}

static void ApplyProjectManifestGlobalState(AppSharedState& shared, const ProjectManifestV11& manifest)
{
    shared.performance.sequencer.seq_running = (manifest.seq_running != 0);
    shared.performance.plocks.plock_apply_enabled = (manifest.plock_apply_enabled != 0);
    shared.performance.modulation.lfo_wave.store(manifest.lfo_wave, std::memory_order_release);
    shared.performance.sequencer.seq_bpm = manifest.seq_bpm;

    shared.performance.macros.macro_ui = manifest.macro_ui;
    shared.performance.macros.macro_ui.selected = manifest.macro_ui.selected;
    Macros_Publish(shared, shared.performance.macros.macro_ui);

    for(size_t i = 0; i < kMaxModRoutes; ++i)
        shared.performance.modulation.mod_routes_ui[i] = manifest.mod_routes[i];
    shared.performance.modulation.mod_route_selected = manifest.mod_route_selected;
    ModMatrix_Publish(shared.performance.modulation.mod_matrix,
                      shared.performance.modulation.mod_routes_ui);
    shared.performance.express.enabled.store(manifest.express_enabled ? 1u : 0u,
                                             std::memory_order_release);
}

static void ApplyProjectManifestLayerState(AppEngineState& engine, const ProjectManifestV11& manifest)
{
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        engine.layer.engine_tune_semitones[slot] = ClampProjectTune(manifest.engine_tune_semitones[slot]);
        engine.layer.engine_tune_cents[slot] = ClampProjectTuneCents(manifest.engine_tune_cents[slot]);
        engine.keyzone.perform_keyzone_lo_note[slot] = manifest.perform_keyzone_lo_note[slot];
        engine.keyzone.perform_keyzone_hi_note[slot] = manifest.perform_keyzone_hi_note[slot];
        ClampProjectKeyzoneRange(engine.keyzone.perform_keyzone_lo_note[slot],
                                 engine.keyzone.perform_keyzone_hi_note[slot]);
        engine.adsr.perform_adsr_row[slot] = ClampProjectAdsrRow(manifest.perform_adsr_row[slot]);
        engine.layer.engine_play_mode[slot] = ClampProjectPlayMode(manifest.engine_play_mode[slot]);
        engine.adsr.perform_adsr_loop_attack[slot] = manifest.perform_adsr_loop_attack[slot];
        if(engine.adsr.perform_adsr_loop_attack[slot] < 2u)
            engine.adsr.perform_adsr_loop_attack[slot] = 2u;
        if(engine.adsr.perform_adsr_loop_attack[slot] > 1000u)
            engine.adsr.perform_adsr_loop_attack[slot] = 1000u;
        engine.adsr.perform_adsr_loop_decay[slot] = manifest.perform_adsr_loop_decay[slot];
        if(engine.adsr.perform_adsr_loop_decay[slot] < 1u)
            engine.adsr.perform_adsr_loop_decay[slot] = 1u;
        if(engine.adsr.perform_adsr_loop_decay[slot] > 100u)
            engine.adsr.perform_adsr_loop_decay[slot] = 100u;
        engine.adsr.perform_adsr_loop_sustain[slot] = manifest.perform_adsr_loop_sustain[slot];
        if(engine.adsr.perform_adsr_loop_sustain[slot] > 100u)
            engine.adsr.perform_adsr_loop_sustain[slot] = 100u;
        engine.adsr.perform_adsr_loop_release[slot] = manifest.perform_adsr_loop_release[slot];
        if(engine.adsr.perform_adsr_loop_release[slot] < 1u)
            engine.adsr.perform_adsr_loop_release[slot] = 1u;
        if(engine.adsr.perform_adsr_loop_release[slot] > 1000u)
            engine.adsr.perform_adsr_loop_release[slot] = 1000u;
        engine.adsr.perform_adsr_loop_crossfade[slot]
            = ClampProjectFloat(manifest.perform_adsr_loop_crossfade[slot], 0.0f, 0.5f);
        engine.adsr.perform_adsr_loop_crossfade_shape[slot]
            = ClampProjectFloat(manifest.perform_adsr_loop_crossfade_shape[slot], 0.0f, 1.0f);
        engine.adsr.perform_adsr_env_a_x[slot] = manifest.perform_adsr_env_a_x[slot];
        engine.adsr.perform_adsr_env_d_x[slot] = manifest.perform_adsr_env_d_x[slot];
        engine.adsr.perform_adsr_env_r_x[slot] = manifest.perform_adsr_env_r_x[slot];
        engine.adsr.perform_adsr_env_s_level[slot] = manifest.perform_adsr_env_s_level[slot];
        ClampProjectAdsrGraph(engine.adsr.perform_adsr_env_a_x[slot],
                              engine.adsr.perform_adsr_env_d_x[slot],
                              engine.adsr.perform_adsr_env_r_x[slot],
                              engine.adsr.perform_adsr_env_s_level[slot]);
        engine.layer.engine_gain_db[slot] = ClampProjectEngineGainDb(manifest.engine_gain_db[slot]);
        engine.layer.engine_drive_mode[slot] = ClampProjectDriveMode(manifest.engine_drive_mode[slot]);
        for(uint8_t row = 0; row < ProjectExpressState::kRowCount; ++row)
            ApplyProjectExpressRow(engine, manifest, slot, row);
        ApplyProjectExpressPolyPorto(engine, manifest, slot);
    }
    ExpressNormalizeAssignments(engine.express.target,
                                engine.express.min_value,
                                engine.express.max_value);
}

void ApplyProjectLoadState(AppEngineState& engine,
                           AppSharedState& shared,
                           Params& params,
                           const ProjectManifestV11& manifest)
{
    ApplyProjectManifestGlobalState(shared, manifest);
    ApplyProjectManifestLayerState(engine, manifest);
    PublishProjectPerformParams(params,
                                engine,
                                manifest.engine_layer_master_level,
                                manifest.fx_order,
                                &manifest.sat,
                                &manifest.eq,
                                &manifest.delay,
                                &manifest.reverb,
                                manifest.engine_filter_cutoff_hz,
                                manifest.engine_filter_resonance);
    SyncProjectProcessVolumeUiState(engine, manifest.engine_layer_master_level);
    SyncProjectProcessFxOrderUiState(engine, manifest.fx_order);
}

void SetupProjectRestoreState(AppWorkerState& worker,
                              AppEngineState& engine,
                              const ProjectManifestV11& manifest)
{
    ClearProjectRestoreState(worker);
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        if(!ProjectManifestHasLayer(manifest, slot))
            continue;

        const uint8_t bit = static_cast<uint8_t>(1u << slot);
        worker.project_restore.project_pending_edit[slot] = manifest.edit[slot];
        worker.project_restore.project_edit_pending_mask |= bit;
        s_sd.project_restore_pending_mask |= bit;
        std::snprintf(s_sd.project_restore_path[slot],
                      sizeof(s_sd.project_restore_path[slot]),
                      "%s",
                      manifest.wav_path[slot]);
        std::snprintf(engine.layer.engine_sample_path[slot],
                      sizeof(engine.layer.engine_sample_path[slot]),
                      "%s",
                      manifest.wav_path[slot]);
        WorkerExtractBaseName(manifest.wav_path[slot],
                              engine.layer.engine_sample_name[slot],
                              sizeof(engine.layer.engine_sample_name[slot]));
    }
}

bool ReadProjectLoadManifest(AppUiState& ui,
                             AppProjectState& project,
                             uint8_t project_slot,
                             ProjectManifestV11& manifest)
{
    if(!EnsureSdMounted(ui))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    char prj_path[kProjectPathMax];
    const char* base = s_sd.fsi.GetSDPath();
    if(!MakeProjectSlotPath(prj_path, sizeof(prj_path), base, project_slot, "AKPRJ"))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    const FRESULT open_res = f_open(&s_sd.file, prj_path, FA_READ);
    if(open_res != FR_OK)
    {
        if(open_res == FR_NO_FILE || open_res == FR_NO_PATH)
            SetProjectSlotStatus(project, project_slot, "EMPTY");
        else
            SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    if(!ReadProjectManifestFromFile(manifest))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    return true;
}

void PrepareProjectLoadManifest(ProjectManifestV11& manifest)
{
    if(manifest.version == 16u)
        manifest.reverb.reverb_fader_mode = 0u;

    SanitizeProjectFxOrder(manifest.fx_order);
    ClampProjectSatState(manifest.sat);
    ClampProjectEqState(manifest.eq);
    ClampProjectDelayState(manifest.delay);
    ClampProjectReverbState(manifest.reverb);
    manifest.version = kProjectManifestVersion;
    for(uint8_t layer = 0; layer < kProjectSampleLayerCount; ++layer)
    {
        for(uint8_t row = 0; row < ProjectExpressState::kRowCount; ++row)
        {
            ExpressClampRow(manifest.express.target[layer][row],
                            manifest.express.min_value[layer][row],
                            manifest.express.max_value[layer][row]);
        }
        ExpressClampPolyPortoConfig(manifest.express.poly_porto_voice_limit[layer],
                                    manifest.express.poly_porto_slide_ms[layer],
                                    manifest.express.poly_porto_source_range_semitones[layer],
                                    manifest.express.poly_porto_source_mode[layer],
                                    manifest.express.poly_porto_release_ms[layer]);
    }
    manifest.express_enabled = manifest.express_enabled ? 1u : 0u;
}

bool BeginProjectRestoreLoad(AppUiState& ui,
                             AppProjectState& project,
                             AppEngineState& engine,
                             AppSharedState& shared,
                             AppWorkerState& worker,
                             uint8_t project_slot)
{
    if(s_sd.project_restore_pending_mask == 0u)
    {
        ui.sd.last_loaded_path[0] = '\0';
        return true;
    }

    if(!StartNextProjectRestoreLoad(ui, engine, worker, shared))
    {
        ClearProjectRestoreState(worker);
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    return true;
}
