#include "ui_worker_internal.h"
#include "ui_worker_project_internal.h"

#include "app_state_engine.h"
#include "app_state_project.h"
#include "app_state_shared.h"
#include "app_state_ui.h"
#include "app_state_worker.h"
#include "sample_edit.h"
#include "params.h"
#include "macros.h"
#include "mod_matrix.h"
#include "express_state.h"
#include "tilt_eq.h"

#include <cstdio>
#include <cstring>

// Project slot / path helpers
static void SetProjectStatus(AppProjectState& project, const char* msg)
{
    if(!msg)
    {
        project.project_status[0] = '\0';
        return;
    }
    std::snprintf(project.project_status, sizeof(project.project_status), "%s", msg);
}

static uint8_t ClampProjectSlotIndex(uint8_t slot)
{
    return (slot < kProjectSlotCount) ? slot : 0u;
}

static bool IsProjectNameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static void SanitizeProjectName(char* out, size_t n, const char* src)
{
    if(!out || n == 0)
        return;

    size_t write = 0;
    if(src)
    {
        for(size_t read = 0; src[read] != '\0' && write + 1 < n; ++read)
        {
            char c = src[read];
            if(!IsProjectNameChar(c))
                continue;
            out[write++] = c;
        }
    }
    out[write] = '\0';
}

uint8_t RequestedProjectSlot(const AppWorkerState& worker)
{
    return ClampProjectSlotIndex(static_cast<uint8_t>(worker.ui_req_arg0));
}

ProjectStyleId RequestedProjectStyle(const AppWorkerState& worker)
{
    return ProjectStyleFromStored(static_cast<uint8_t>(worker.ui_req_arg1));
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

bool MakeProjectSlotPath(char* out, size_t n, const char* base, uint8_t slot, const char* ext)
{
    char name[20];
    if(!MakeProjectSlotFilename(name, sizeof(name), slot, ext))
        return false;
    return MakePath(out, n, base, name);
}

void SetProjectSlotStatus(AppProjectState& project, uint8_t slot, const char* msg)
{
    char status[sizeof(project.project_status)];
    const uint8_t slot_num = static_cast<uint8_t>(ClampProjectSlotIndex(slot) + 1u);
    std::snprintf(status, sizeof(status), "P%02u %s", slot_num, msg ? msg : "");
    SetProjectStatus(project, status);
}

static void CacheProjectSlotName(AppProjectState& project, uint8_t slot, const char* name)
{
    if(slot >= kProjectSlotCount)
        return;
    SanitizeProjectName(project.slot_names[slot], sizeof(project.slot_names[slot]), name);
}

static void CacheProjectSlotStyle(AppProjectState& project, uint8_t slot, ProjectStyleId style)
{
    if(slot >= kProjectSlotCount)
        return;
    project.slot_styles[slot] = style;
}

static void ClearProjectSlotMetadata(AppProjectState& project, uint8_t slot)
{
    if(slot >= kProjectSlotCount)
        return;
    project.slot_has_file[slot] = false;
    project.slot_names[slot][0] = '\0';
    project.slot_styles[slot] = ProjectStyleId::None;
}

static bool ReadExistingProjectMetadata(uint8_t slot,
                                        char* out_name,
                                        size_t out_name_n,
                                        ProjectStyleId* out_style)
{
    if(!out_name || out_name_n == 0)
        return false;

    out_name[0] = '\0';
    if(out_style)
        *out_style = ProjectStyleId::None;
    const char* base = s_sd.fsi.GetSDPath();
    char prj_path[kProjectPathMax];
    if(!MakeProjectSlotPath(prj_path, sizeof(prj_path), base, slot, "AKPRJ"))
        return false;

    if(f_open(&s_sd.file, prj_path, FA_READ) != FR_OK)
        return false;

    ProjectManifestV11 manifest{};
    if(!ReadProjectManifestFromFile(manifest))
        return false;

    SanitizeProjectName(out_name, out_name_n, manifest.project_name);
    if(out_style)
        *out_style = ProjectStyleFromStored(manifest.project_style);
    return true;
}

bool ProjectManifestHasLayer(const ProjectManifestV11& m, uint8_t layer)
{
    if(layer >= kProjectSampleLayerCount)
        return false;
    const uint8_t bit = static_cast<uint8_t>(1u << layer);
    return (m.sample_present_mask & bit) != 0u && m.wav_path[layer][0] != '\0';
}

// Project clamp helpers
float ClampProjectFloat(float value, float lo, float hi)
{
    if(value < lo)
        value = lo;
    if(value > hi)
        value = hi;
    return value;
}

int8_t ClampProjectTune(int value)
{
    if(value < -24)
        value = -24;
    if(value > 24)
        value = 24;
    return static_cast<int8_t>(value);
}

int8_t ClampProjectTuneCents(int value)
{
    if(value < -99)
        value = -99;
    if(value > 99)
        value = 99;
    return static_cast<int8_t>(value);
}

uint8_t ClampProjectMidiNote(int value)
{
    if(value < 0)
        value = 0;
    if(value > 127)
        value = 127;
    return static_cast<uint8_t>(value);
}

void ClampProjectKeyzoneRange(uint8_t& lo, uint8_t& hi)
{
    lo = ClampProjectMidiNote(lo);
    hi = ClampProjectMidiNote(hi);
    if(lo > hi)
        hi = lo;
}

uint8_t ClampProjectAdsrRow(int value)
{
    if(value < 0)
        value = 0;
    if(value > 2)
        value = 2;
    return static_cast<uint8_t>(value);
}

uint8_t ClampProjectPlayMode(int value)
{
    return (value != 0) ? 1u : 0u;
}

int16_t ClampProjectEngineGainDb(int value)
{
    if(value < 0)
        value = 0;
    if(value > 60)
        value = 60;
    return static_cast<int16_t>(value);
}

uint8_t ClampProjectDriveMode(int value)
{
    return (value != 0) ? 1u : 0u;
}

uint8_t ClampProjectSatMode(int value)
{
    return (value != 0) ? 1u : 0u;
}

float ClampProjectFilterCutoffHz(float value)
{
    if(value < 20.0f)
        value = 20.0f;
    if(value > 20000.0f)
        value = 20000.0f;
    return value;
}

void ClampProjectAdsrGraph(uint8_t& a_x, uint8_t& d_x, uint8_t& r_x, uint8_t& s_level)
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
void ClampProjectSatState(ProjectSatState& sat)
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

void ClampProjectDelayState(ProjectDelayState& delay)
{
    delay.delay_on = (delay.delay_on != 0u) ? 1u : 0u;
    delay.delay_mix = ClampProjectFloat(delay.delay_mix, 0.0f, 1.0f);
    delay.delay_time_l = ClampProjectFloat(delay.delay_time_l, 0.0f, 1.0f);
    delay.delay_time_r = ClampProjectFloat(delay.delay_time_r, 0.0f, 1.0f);
    delay.delay_feedback = ClampProjectFloat(delay.delay_feedback, 0.0f, 1.0f);
}

void ClampProjectReverbState(ProjectReverbState& reverb)
{
    reverb.reverb_on = (reverb.reverb_on != 0u) ? 1u : 0u;
    reverb.reverb_fader_mode = (reverb.reverb_fader_mode == 0u) ? 0u : 1u;
    reverb.reverb_mix = ClampProjectFloat(reverb.reverb_mix, 0.0f, 1.0f);
    reverb.reverb_pre = ClampProjectFloat(reverb.reverb_pre, 0.0f, 1.0f);
    reverb.reverb_damp = ClampProjectFloat(reverb.reverb_damp, 0.0f, 1.0f);
    reverb.reverb_decay = ClampProjectFloat(reverb.reverb_decay, 0.0f, 1.0f);
    reverb.reverb_mod = ClampProjectFloat(reverb.reverb_mod, 0.0f, 1.0f);
}

static void ClampProjectExpressRow(uint8_t& target, uint16_t& min_value, uint16_t& max_value)
{
    ExpressClampRow(target, min_value, max_value);
}

static void ClampProjectExpressPolyPorto(ProjectExpressState& express, uint8_t layer)
{
    ExpressClampPolyPortoConfig(express.poly_porto_voice_limit[layer],
                                express.poly_porto_slide_ms[layer],
                                express.poly_porto_source_range_semitones[layer],
                                express.poly_porto_source_mode[layer],
                                express.poly_porto_release_ms[layer]);
}

void ClampProjectEqState(ProjectEqState& eq)
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

void SanitizeProjectFxOrder(uint8_t* fx_order)
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

static void CollectProjectLayerState(ProjectManifestV11& manifest,
                                     const AppEngineState& engine,
                                     const AppSharedState& shared,
                                     const PerformParamsTargets& targets)
{
    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
    {
        const Sample& sample = shared.sample.publish.sd_slots[slot];
        const char* path = engine.layer.engine_sample_path[slot];
        if(sample.pcm == nullptr || sample.length == 0 || !path || path[0] == '\0')
            continue;

        const uint8_t bit = static_cast<uint8_t>(1u << slot);
        manifest.sample_present_mask |= bit;
        std::snprintf(manifest.wav_path[slot], sizeof(manifest.wav_path[slot]), "%s", path);

        SampleEdit edit = shared.sample.edit.sd_edit_slots[slot];
        SampleEdit_Clamp(edit, sample.length);
        manifest.edit[slot] = edit;
        manifest.engine_tune_semitones[slot] = ClampProjectTune(engine.layer.engine_tune_semitones[slot]);
        manifest.engine_tune_cents[slot] = ClampProjectTuneCents(engine.layer.engine_tune_cents[slot]);
        manifest.perform_keyzone_lo_note[slot] = engine.keyzone.perform_keyzone_lo_note[slot];
        manifest.perform_keyzone_hi_note[slot] = engine.keyzone.perform_keyzone_hi_note[slot];
        manifest.perform_adsr_row[slot] = ClampProjectAdsrRow(engine.adsr.perform_adsr_row[slot]);
        manifest.engine_play_mode[slot] = ClampProjectPlayMode(engine.layer.engine_play_mode[slot]);
        manifest.perform_adsr_loop_attack[slot] = engine.adsr.perform_adsr_loop_attack[slot];
        manifest.perform_adsr_loop_decay[slot] = engine.adsr.perform_adsr_loop_decay[slot];
        manifest.perform_adsr_loop_sustain[slot] = engine.adsr.perform_adsr_loop_sustain[slot];
        manifest.perform_adsr_loop_release[slot] = engine.adsr.perform_adsr_loop_release[slot];
        manifest.perform_adsr_loop_crossfade[slot] = engine.adsr.perform_adsr_loop_crossfade[slot];
        manifest.perform_adsr_loop_crossfade_shape[slot] = engine.adsr.perform_adsr_loop_crossfade_shape[slot];
        manifest.perform_adsr_env_a_x[slot] = engine.adsr.perform_adsr_env_a_x[slot];
        manifest.perform_adsr_env_d_x[slot] = engine.adsr.perform_adsr_env_d_x[slot];
        manifest.perform_adsr_env_r_x[slot] = engine.adsr.perform_adsr_env_r_x[slot];
        manifest.perform_adsr_env_s_level[slot] = engine.adsr.perform_adsr_env_s_level[slot];
        manifest.engine_gain_db[slot] = engine.layer.engine_gain_db[slot];
        manifest.engine_drive_mode[slot] = engine.layer.engine_drive_mode[slot];
        manifest.engine_filter_cutoff_hz[slot] = targets.engine_filter_cutoff_hz[slot];
        manifest.engine_filter_resonance[slot] = targets.engine_filter_resonance[slot];
    }

    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
    {
        manifest.engine_layer_master_level[slot]
            = ClampProjectFloat(targets.engine_layer_master_level[slot], 0.0f, 2.0f);
        manifest.engine_tune_cents[slot] = ClampProjectTuneCents(engine.layer.engine_tune_cents[slot]);
        if((manifest.sample_present_mask & static_cast<uint8_t>(1u << slot)) == 0u)
            manifest.engine_tune_semitones[slot] = ClampProjectTune(engine.layer.engine_tune_semitones[slot]);
        manifest.perform_keyzone_lo_note[slot] = engine.keyzone.perform_keyzone_lo_note[slot];
        manifest.perform_keyzone_hi_note[slot] = engine.keyzone.perform_keyzone_hi_note[slot];
        ClampProjectKeyzoneRange(manifest.perform_keyzone_lo_note[slot],
                                 manifest.perform_keyzone_hi_note[slot]);
        manifest.perform_adsr_row[slot] = ClampProjectAdsrRow(engine.adsr.perform_adsr_row[slot]);
        manifest.engine_play_mode[slot] = ClampProjectPlayMode(engine.layer.engine_play_mode[slot]);
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
        for(uint8_t row = 0; row < ProjectExpressState::kRowCount; ++row)
        {
            manifest.express.target[slot][row] = engine.express.target[slot][row];
            manifest.express.min_value[slot][row] = engine.express.min_value[slot][row];
            manifest.express.max_value[slot][row] = engine.express.max_value[slot][row];
            ClampProjectExpressRow(manifest.express.target[slot][row],
                                   manifest.express.min_value[slot][row],
                                   manifest.express.max_value[slot][row]);
        }
        manifest.express.poly_porto_voice_limit[slot] = engine.express.poly_porto_voice_limit[slot];
        manifest.express.poly_porto_slide_ms[slot] = engine.express.poly_porto_slide_ms[slot];
        manifest.express.poly_porto_source_range_semitones[slot]
            = engine.express.poly_porto_source_range_semitones[slot];
        manifest.express.poly_porto_source_mode[slot] = engine.express.poly_porto_source_mode[slot];
        manifest.express.poly_porto_release_ms[slot] = engine.express.poly_porto_release_ms[slot];
        ClampProjectExpressPolyPorto(manifest.express, slot);
    }
}

static void CollectProjectGlobalState(ProjectManifestV11& manifest,
                                      const AppSharedState& shared,
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

    manifest.delay.delay_on = targets.delay_on ? 1u : 0u;
    manifest.delay.delay_mix = targets.delay_mix;
    manifest.delay.delay_time_l = targets.delay_time_l;
    manifest.delay.delay_time_r = targets.delay_time_r;
    manifest.delay.delay_feedback = targets.delay_feedback;
    ClampProjectDelayState(manifest.delay);
    manifest.reverb.reverb_on = targets.reverb_on ? 1u : 0u;
    manifest.reverb.reverb_fader_mode = targets.reverb_fader_mode;
    manifest.reverb.reverb_mix = targets.reverb_mix;
    manifest.reverb.reverb_pre = targets.reverb_pre;
    manifest.reverb.reverb_damp = targets.reverb_damp;
    manifest.reverb.reverb_decay = targets.reverb_decay;
    manifest.reverb.reverb_mod = targets.reverb_mod;
    ClampProjectReverbState(manifest.reverb);
    manifest.seq_running = shared.performance.sequencer.seq_running ? 1 : 0;
    manifest.plock_apply_enabled = shared.performance.plocks.plock_apply_enabled ? 1 : 0;
    manifest.lfo_wave = shared.performance.modulation.lfo_wave.load(std::memory_order_relaxed);
    manifest.seq_bpm = shared.performance.sequencer.seq_bpm;
    manifest.macro_ui = shared.performance.macros.macro_ui;
    manifest.macro_sel = shared.performance.macros.macro_sel.load(std::memory_order_relaxed) & 1u;
    for(size_t i = 0; i < kMaxModRoutes; ++i)
        manifest.mod_routes[i] = shared.performance.modulation.mod_routes_ui[i];
    manifest.mod_route_selected = shared.performance.modulation.mod_route_selected;
    manifest.express_enabled
        = shared.performance.express.enabled.load(std::memory_order_relaxed) ? 1u : 0u;
}

bool ScanProjectSlots(AppUiState& ui, AppProjectState& project)
{
    project.metadata_scan_requested = false;
    project.metadata_scan_complete = false;

    if(!EnsureSdMounted(ui))
        return false;

    for(uint8_t slot = 0; slot < kProjectSlotCount; ++slot)
    {
        project.slot_has_file[slot] = false;
        project.slot_names[slot][0] = '\0';
        project.slot_styles[slot] = ProjectStyleId::None;

        const char* base = s_sd.fsi.GetSDPath();
        char prj_path[kProjectPathMax];
        if(!MakeProjectSlotPath(prj_path, sizeof(prj_path), base, slot, "AKPRJ"))
            continue;

        const FRESULT open_res = f_open(&s_sd.file, prj_path, FA_READ);
        if(open_res != FR_OK)
            continue;

        project.slot_has_file[slot] = true;
        ProjectManifestV11 manifest{};
        if(!ReadProjectManifestFromFile(manifest))
            continue;

        CacheProjectSlotName(project, slot, manifest.project_name);
        CacheProjectSlotStyle(project, slot, ProjectStyleFromStored(manifest.project_style));
    }

    project.metadata_scan_complete = true;
    ui.ui_dirty = true;
    return true;
}

static bool WriteProjectManifestFile(uint8_t project_slot, const ProjectManifestV11& manifest)
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

static uint8_t BeginProjectSave(AppProjectState& project, const AppWorkerState& worker)
{
    const uint8_t project_slot = RequestedProjectSlot(worker);
    SetProjectSlotStatus(project, project_slot, "SAVING");
    return project_slot;
}

static bool EnsureProjectSaveMounted(AppUiState& ui, AppProjectState& project, uint8_t project_slot)
{
    if(EnsureSdMounted(ui))
        return true;

    SetProjectSlotStatus(project, project_slot, "ERR");
    return false;
}

static void CollectProjectSaveManifest(ProjectManifestV11& manifest,
                                       const AppUiState& ui,
                                       AppProjectState& project,
                                       uint8_t project_slot,
                                       AppEngineState& engine,
                                       AppSharedState& shared,
                                       const Params& params)
{
    const PerformParamsTargets& targets = params.TargetsForUI();
    CollectProjectLayerState(manifest, engine, shared, targets);
    CollectProjectGlobalState(manifest, shared, targets);

    char project_name[sizeof(manifest.project_name)] = {};
    ProjectStyleId project_style = ProjectStyleId::None;
    if(project_slot < kProjectSlotCount)
    {
        ProjectStyleId existing_style = ProjectStyleId::None;
        char existing_name[sizeof(manifest.project_name)] = {};
        const bool have_existing_metadata = project.slot_has_file[project_slot]
                                            && ReadExistingProjectMetadata(project_slot,
                                                                          existing_name,
                                                                          sizeof(existing_name),
                                                                          &existing_style);
        if(ui.pending_named_save_active && ui.pending_named_save_slot == project_slot)
        {
            SanitizeProjectName(project_name, sizeof(project_name), ui.pending_named_save_name);
        }
        else if(project.slot_names[project_slot][0] != '\0')
        {
            SanitizeProjectName(project_name, sizeof(project_name), project.slot_names[project_slot]);
        }
        else if(project.slot_has_file[project_slot])
        {
            if(have_existing_metadata)
                std::snprintf(project_name, sizeof(project_name), "%s", existing_name);
        }

        if(project.slot_has_file[project_slot])
        {
            project_style = project.slot_styles[project_slot];
            if(project_style == ProjectStyleId::None && have_existing_metadata)
                project_style = existing_style;
        }
    }
    std::snprintf(manifest.project_name, sizeof(manifest.project_name), "%s", project_name);
    manifest.project_style = ProjectStyleToStored(project_style);
}

static bool ValidateProjectSaveManifest(AppProjectState& project,
                                        uint8_t project_slot,
                                        const ProjectManifestV11& manifest)
{
    if(manifest.sample_present_mask != 0u)
        return true;

    SetProjectSlotStatus(project, project_slot, "ERR");
    return false;
}

static bool WriteProjectSaveManifest(AppProjectState& project,
                                     uint8_t project_slot,
                                     const ProjectManifestV11& manifest)
{
    if(!WriteProjectManifestFile(project_slot, manifest))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    SetProjectSlotStatus(project, project_slot, "SAVED");
    return true;
}

bool SaveProject(AppUiState& ui,
                 AppProjectState& project,
                 AppEngineState& engine,
                 AppSharedState& shared,
                 AppWorkerState& worker,
                 const Params& params)
{
    const uint8_t project_slot = BeginProjectSave(project, worker);
    if(!EnsureProjectSaveMounted(ui, project, project_slot))
        return false;

    ProjectManifestV11 manifest{};
    CollectProjectSaveManifest(manifest, ui, project, project_slot, engine, shared, params);
    if(!ValidateProjectSaveManifest(project, project_slot, manifest))
        return false;

    if(!WriteProjectSaveManifest(project, project_slot, manifest))
        return false;

    CacheProjectSlotName(project, project_slot, manifest.project_name);
    CacheProjectSlotStyle(project, project_slot, ProjectStyleFromStored(manifest.project_style));
    project.slot_has_file[project_slot] = true;
    project.metadata_scan_complete = true;
    project.has_active_project_slot = true;
    project.active_project_slot = project_slot;
    return true;
}

static uint8_t BeginProjectLoad(AppProjectState& project, const AppWorkerState& worker)
{
    const uint8_t project_slot = RequestedProjectSlot(worker);
    SetProjectSlotStatus(project, project_slot, "LOADING");
    return project_slot;
}

bool LoadProject(AppUiState& ui,
                 AppProjectState& project,
                 AppEngineState& engine,
                 AppSharedState& shared,
                 AppWorkerState& worker,
                 Params& params)
{
    const uint8_t project_slot = BeginProjectLoad(project, worker);
    ProjectManifestV11 manifest{};
    if(!ReadProjectLoadManifest(ui, project, project_slot, manifest))
        return false;

    PrepareProjectLoadManifest(manifest);
    CacheProjectSlotName(project, project_slot, manifest.project_name);
    CacheProjectSlotStyle(project, project_slot, ProjectStyleFromStored(manifest.project_style));
    project.slot_has_file[project_slot] = true;
    project.metadata_scan_complete = true;
    ApplyProjectLoadState(engine, shared, params, manifest);

    SetupProjectRestoreState(worker, engine, manifest);
    return BeginProjectRestoreLoad(ui, project, engine, shared, worker, project_slot);
}

bool RenameProject(AppUiState& ui, AppProjectState& project, AppWorkerState& worker)
{
    const uint8_t project_slot = RequestedProjectSlot(worker);
    if(project_slot >= kProjectSlotCount || !project.slot_has_file[project_slot])
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    if(!EnsureSdMounted(ui))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    const char* base = s_sd.fsi.GetSDPath();
    char prj_path[kProjectPathMax];
    if(!MakeProjectSlotPath(prj_path, sizeof(prj_path), base, project_slot, "AKPRJ"))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    if(f_open(&s_sd.file, prj_path, FA_READ) != FR_OK)
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    ProjectManifestV11 manifest{};
    if(!ReadProjectManifestFromFile(manifest))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    SanitizeProjectName(manifest.project_name,
                        sizeof(manifest.project_name),
                        project.pending_rename_name);
    if(!WriteProjectManifestFile(project_slot, manifest))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    CacheProjectSlotName(project, project_slot, manifest.project_name);
    CacheProjectSlotStyle(project, project_slot, ProjectStyleFromStored(manifest.project_style));
    project.slot_has_file[project_slot] = true;
    project.metadata_scan_complete = true;
    SetProjectSlotStatus(project, project_slot, "RENAMED");
    ui.ui_dirty = true;
    return true;
}

bool DeleteProject(AppUiState& ui, AppProjectState& project, AppWorkerState& worker)
{
    const uint8_t project_slot = RequestedProjectSlot(worker);
    if(project_slot >= kProjectSlotCount || !project.slot_has_file[project_slot])
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    if(!EnsureSdMounted(ui))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    const char* base = s_sd.fsi.GetSDPath();
    char prj_path[kProjectPathMax];
    if(!MakeProjectSlotPath(prj_path, sizeof(prj_path), base, project_slot, "AKPRJ"))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    if(f_unlink(prj_path) != FR_OK)
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    ClearProjectSlotMetadata(project, project_slot);
    project.metadata_scan_complete = true;
    SetProjectSlotStatus(project, project_slot, "DELETED");
    ui.ui_dirty = true;
    return true;
}

bool UpdateProjectStyle(AppUiState& ui, AppProjectState& project, AppWorkerState& worker)
{
    const uint8_t project_slot = RequestedProjectSlot(worker);
    if(project_slot >= kProjectSlotCount || !project.slot_has_file[project_slot])
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    if(!EnsureSdMounted(ui))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    const char* base = s_sd.fsi.GetSDPath();
    char prj_path[kProjectPathMax];
    if(!MakeProjectSlotPath(prj_path, sizeof(prj_path), base, project_slot, "AKPRJ"))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    if(f_open(&s_sd.file, prj_path, FA_READ) != FR_OK)
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    ProjectManifestV11 manifest{};
    if(!ReadProjectManifestFromFile(manifest))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    manifest.project_style = ProjectStyleToStored(RequestedProjectStyle(worker));
    if(!WriteProjectManifestFile(project_slot, manifest))
    {
        SetProjectSlotStatus(project, project_slot, "ERR");
        return false;
    }

    CacheProjectSlotName(project, project_slot, manifest.project_name);
    CacheProjectSlotStyle(project, project_slot, ProjectStyleFromStored(manifest.project_style));
    project.slot_has_file[project_slot] = true;
    project.metadata_scan_complete = true;
    SetProjectSlotStatus(project, project_slot, "STYLED");
    ui.ui_dirty = true;
    return true;
}
