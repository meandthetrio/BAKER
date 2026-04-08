#include "ui_worker.h"

#include "app_state.h"
#include "sd_browser_state.h"
#include "sd_sample_pool.h"
#include "ui_requests.h"
#include "sample_edit.h"
#include "project_manifest.h"
#include "params.h"
#include "macros.h"
#include "mod_matrix.h"

#include "fatfs.h"
#include "ff.h"
#include "per/sdmmc.h"

#include <cstring>
#include <cstdio>

using namespace daisy;

static volatile uint32_t s_work_sink = 0;

static uint16_t ReadU16LE(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t ReadU32LE(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static void WriteU16LE(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}

static void WriteU32LE(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

struct WavInfo
{
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
};

enum class LoaderState : uint8_t
{
    Idle,
    Scan,
    Load,
};

static bool ParseWavHeader(FIL& file, WavInfo& info)
{
    uint8_t hdr[12];
    UINT br = 0;
    if(f_lseek(&file, 0) != FR_OK)
        return false;
    if(f_read(&file, hdr, sizeof(hdr), &br) != FR_OK || br != sizeof(hdr))
        return false;
    if(std::memcmp(hdr, "RIFF", 4) != 0)
        return false;
    if(std::memcmp(hdr + 8, "WAVE", 4) != 0)
        return false;

    bool have_fmt = false;
    bool have_data = false;
    for(int chunk = 0; chunk < 16 && !have_data; ++chunk)
    {
        uint8_t chdr[8];
        if(f_read(&file, chdr, sizeof(chdr), &br) != FR_OK || br != sizeof(chdr))
            return false;
        const uint32_t csize = ReadU32LE(chdr + 4);
        const uint32_t data_start = f_tell(&file);

        if(std::memcmp(chdr, "fmt ", 4) == 0)
        {
            uint8_t fmt[16];
            if(csize < sizeof(fmt))
                return false;
            if(f_read(&file, fmt, sizeof(fmt), &br) != FR_OK || br != sizeof(fmt))
                return false;
            info.audio_format = ReadU16LE(fmt + 0);
            info.channels = ReadU16LE(fmt + 2);
            info.sample_rate = ReadU32LE(fmt + 4);
            info.bits_per_sample = ReadU16LE(fmt + 14);
            have_fmt = true;

            uint32_t remaining = csize - sizeof(fmt);
            if(remaining > 0)
                f_lseek(&file, data_start + csize);
        }
        else if(std::memcmp(chdr, "data", 4) == 0)
        {
            info.data_offset = data_start;
            info.data_size = csize;
            have_data = true;
            break;
        }
        else
        {
            f_lseek(&file, data_start + csize);
        }

        if(csize & 1u)
            f_lseek(&file, f_tell(&file) + 1);
    }

    return have_fmt && have_data;
}

static bool WriteWavHeader(FIL& file, uint32_t frames)
{
    const uint32_t data_bytes = frames * 2u;
    const uint32_t riff_size = 36u + data_bytes;
    uint8_t hdr[44];
    std::memset(hdr, 0, sizeof(hdr));
    std::memcpy(hdr + 0, "RIFF", 4);
    WriteU32LE(hdr + 4, riff_size);
    std::memcpy(hdr + 8, "WAVE", 4);
    std::memcpy(hdr + 12, "fmt ", 4);
    WriteU32LE(hdr + 16, 16u);
    WriteU16LE(hdr + 20, 1u);
    WriteU16LE(hdr + 22, 1u);
    WriteU32LE(hdr + 24, 48000u);
    WriteU32LE(hdr + 28, 48000u * 2u);
    WriteU16LE(hdr + 32, 2u);
    WriteU16LE(hdr + 34, 16u);
    std::memcpy(hdr + 36, "data", 4);
    WriteU32LE(hdr + 40, data_bytes);

    if(f_lseek(&file, 0) != FR_OK)
        return false;
    UINT bw = 0;
    if(f_write(&file, hdr, sizeof(hdr), &bw) != FR_OK || bw != sizeof(hdr))
        return false;
    return true;
}

static bool IsWavName(const char* name)
{
    if(!name)
        return false;
    const size_t len = std::strlen(name);
    if(len < 4)
        return false;
    const char c0 = name[len - 4];
    const char c1 = name[len - 3];
    const char c2 = name[len - 2];
    const char c3 = name[len - 1];
    return (c0 == '.' && (c1 == 'W' || c1 == 'w') && (c2 == 'A' || c2 == 'a')
            && (c3 == 'V' || c3 == 'v'));
}

static bool MakePath(char* out, size_t n, const char* base, const char* name)
{
    if(!out || n == 0 || !base || !name)
        return false;

    const size_t blen = std::strlen(base);
    const bool has_sep = (blen > 0 && base[blen - 1] == '/');
    int r = 0;
    if(has_sep)
        r = std::snprintf(out, n, "%s%s", base, name);
    else
        r = std::snprintf(out, n, "%s/%s", base, name);

    if(r < 0 || r >= static_cast<int>(n))
    {
        out[n - 1] = '\0';
        return false;
    }
    return true;
}

static void ExtractBaseName(const char* path, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;
    out[0] = '\0';
    if(!path || path[0] == '\0')
        return;

    const char* name = path;
    for(const char* p = path; *p; ++p)
    {
        if(*p == '/' || *p == '\\')
            name = p + 1;
    }
    std::snprintf(out, out_n, "%s", name);
}

static constexpr uint32_t kSaveChunkFrames = 2048;

static void SetProjectStatus(AppState& app, const char* msg)
{
    if(!msg)
    {
        app.project_status[0] = '\0';
        return;
    }
    std::snprintf(app.project_status, sizeof(app.project_status), "%s", msg);
}

static uint8_t ClampProjectSlotIndex(uint8_t slot)
{
    return (slot < kProjectSlotCount) ? slot : 0u;
}

static uint8_t RequestedProjectSlot(const AppState& app)
{
    return ClampProjectSlotIndex(static_cast<uint8_t>(app.ui_req_arg0));
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

static void SetProjectSlotStatus(AppState& app, uint8_t slot, const char* msg)
{
    char status[sizeof(app.project_status)];
    const uint8_t slot_num = static_cast<uint8_t>(ClampProjectSlotIndex(slot) + 1u);
    std::snprintf(status, sizeof(status), "P%02u %s", slot_num, msg ? msg : "");
    SetProjectStatus(app, status);
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
           && m.version == kProjectManifestVersion;
}

static bool ProjectManifestValid(const ProjectManifestV1& m)
{
    return std::memcmp(m.magic, "AKPJ", 4) == 0 && m.version == 1u;
}

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

static bool ProjectManifestHasLayer(const ProjectManifestV7& m, uint8_t layer)
{
    if(layer >= kProjectSampleLayerCount)
        return false;
    const uint8_t bit = static_cast<uint8_t>(1u << layer);
    return (m.sample_present_mask & bit) != 0u && m.wav_path[layer][0] != '\0';
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

static float ClampProjectFilterCutoffHz(float value)
{
    if(value < 20.0f)
        value = 20.0f;
    if(value > 20000.0f)
        value = 20000.0f;
    return value;
}

static float ClampProjectFloat(float value, float lo, float hi)
{
    if(value < lo)
        value = lo;
    if(value > hi)
        value = hi;
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

static void PublishProjectPerformParams(Params& params,
                                        const AppState& app,
                                        const float* process_layer_master_level = nullptr,
                                        const float* emphasis_cutoff_hz = nullptr,
                                        const float* emphasis_resonance = nullptr)
{
    PerformParamsTargets& t = params.EditTargets();
    for(uint8_t layer = 0; layer < kProjectSampleLayerCount; ++layer)
    {
        t.engine_tune_semitones[layer] = static_cast<float>(app.engine_tune_semitones[layer]);
        if(process_layer_master_level)
        {
            t.engine_layer_master_level[layer]
                = ClampProjectFloat(process_layer_master_level[layer], 0.0f, 2.0f);
        }
        t.engine_gain_db[layer] = static_cast<float>(app.engine_gain_db[layer]);
        t.engine_drive_mode[layer] = ClampProjectDriveMode(app.engine_drive_mode[layer]);
        if(emphasis_cutoff_hz)
            t.engine_filter_cutoff_hz[layer] = ClampProjectFilterCutoffHz(emphasis_cutoff_hz[layer]);
        if(emphasis_resonance)
            t.engine_filter_resonance[layer] = ClampProjectFloat(emphasis_resonance[layer], 0.0f, 1.0f);
        t.perform_keyzone_lo_note[layer] = app.perform_keyzone_lo_note[layer];
        t.perform_keyzone_hi_note[layer] = app.perform_keyzone_hi_note[layer];
        t.engine_loop_mode[layer] = (app.engine_play_mode[layer] != 0);
        t.engine_loop_attack_ms[layer] = static_cast<float>(app.perform_adsr_loop_attack[layer]);
        t.engine_loop_decay_ms[layer] = static_cast<float>(app.perform_adsr_loop_decay[layer]);
        t.engine_loop_sustain_level[layer]
            = static_cast<float>(app.perform_adsr_loop_sustain[layer]) * 0.01f;
        t.engine_loop_release_ms[layer] = static_cast<float>(app.perform_adsr_loop_release[layer]);
        t.engine_loop_crossfade_amount[layer] = app.perform_adsr_loop_crossfade[layer];
        t.engine_loop_crossfade_shape[layer] = app.perform_adsr_loop_crossfade_shape[layer];
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
        app.perform_process_vol_muted[layer] = false;
        app.perform_process_vol_unmuted_level[layer] = level;
        app.perform_process_vol_pct[layer] = static_cast<uint16_t>(level * 100.0f + 0.5f);
    }
}

struct SdWorkerState
{
    SdmmcHandler sdmmc;
    FatFSInterface fsi;
    bool inited = false;
    bool mounted = false;
    DIR dir{};
    bool dir_open = false;
    FIL file{};
    bool file_open = false;
    char scan_path[kSdPathMax] = {};
    uint32_t data_size = 0;
    uint32_t bytes_loaded = 0;
    uint32_t sample_frames = 0;
    uint16_t load_index = 0;
    uint8_t loading_slot = 0;
    LoaderState state = LoaderState::Idle;
    bool norm_active = false;
    uint8_t norm_slot = 0;
    uint32_t norm_pos = 0;
    uint32_t norm_start = 0;
    uint32_t norm_end = 0;
    int32_t norm_peak = 0;
    bool save_active = false;
    uint8_t save_slot = 0;
    uint32_t save_start = 0;
    uint32_t save_end = 0;
    uint32_t save_total = 0;
    uint32_t save_written = 0;
    float save_gain = 1.0f;
    char save_dir[kSdPathMax] = {};
    char save_path[kSdPathMax] = {};
    char save_name[kSdNameMax] = {};
    uint8_t project_restore_pending_mask = 0;
    char project_restore_path[kSdSampleSlots][kProjectPathMax] = {};
};

static SdWorkerState s_sd;
static bool StartLoadPath(AppState& app, const char* path, uint8_t target_slot);
static void ClearProjectRestoreState(AppState& app);
static bool StartNextProjectRestoreLoad(AppState& app);

static bool EnsureSdMounted(AppState& app)
{
    SdBrowserState& sd = app.sd;
    if(!s_sd.inited)
    {
        SdmmcHandler::Config sd_cfg;
        sd_cfg.Defaults();
        s_sd.sdmmc.Init(sd_cfg);
        FatFSInterface::Config fsi_cfg;
        fsi_cfg.media = FatFSInterface::Config::MEDIA_SD;
        s_sd.fsi.Init(fsi_cfg);
        s_sd.inited = true;
        sd.sd_inited = true;
    }

    FRESULT res = f_mount(&s_sd.fsi.GetSDFileSystem(), s_sd.fsi.GetSDPath(), 1);
    if(res != FR_OK)
    {
        sd.sd_ok = false;
        SdBrowser_SetStatus(sd, "SD ERR");
        return false;
    }
    sd.sd_ok = true;
    return true;
}

static void CancelScan(AppState& app)
{
    SdBrowserState& sd = app.sd;
    if(s_sd.dir_open)
    {
        f_closedir(&s_sd.dir);
        s_sd.dir_open = false;
    }
    s_sd.state = LoaderState::Idle;
    sd.scan_in_progress = false;
    sd.scan_done = true;
}

static void CancelLoad(AppState& app)
{
    SdBrowserState& sd = app.sd;
    if(s_sd.file_open)
    {
        f_close(&s_sd.file);
        s_sd.file_open = false;
    }
    s_sd.state = LoaderState::Idle;
    sd.load_in_progress = false;
    sd.load_progress = 0;
    ClearProjectRestoreState(app);
}

static void FinishRequest(AppState& app)
{
    app.ui_req_busy = false;
    app.ui_req_active = UiReqType::None;
    app.ui_req_progress = 100;
    app.ui_req_done_count++;
    s_sd.state = LoaderState::Idle;
    ClearProjectRestoreState(app);
}

static void ClearProjectRestoreState(AppState& app)
{
    s_sd.project_restore_pending_mask = 0;
    app.project_edit_pending_mask = 0;
    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
        s_sd.project_restore_path[slot][0] = '\0';
}

static bool StartNextProjectRestoreLoad(AppState& app)
{
    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
    {
        const uint8_t bit = static_cast<uint8_t>(1u << slot);
        if((s_sd.project_restore_pending_mask & bit) == 0u)
            continue;
        if(s_sd.project_restore_path[slot][0] == '\0')
        {
            s_sd.project_restore_pending_mask &= static_cast<uint8_t>(~bit);
            app.project_edit_pending_mask &= static_cast<uint8_t>(~bit);
            continue;
        }

        app.perform_layer = slot;
        if(!StartLoadPath(app, s_sd.project_restore_path[slot], slot))
            return false;

        s_sd.project_restore_pending_mask &= static_cast<uint8_t>(~bit);
        return true;
    }
    return false;
}

static bool StartScan(AppState& app)
{
    SdBrowserState& sd = app.sd;
    SdBrowser_ClearList(sd);
    sd.scan_in_progress = true;
    sd.scan_done = false;
    SdBrowser_SetStatus(sd, "SCANNING");
    sd.load_progress = 0;
    sd.load_in_progress = false;
    s_sd.state = LoaderState::Scan;

    if(!EnsureSdMounted(app))
    {
        sd.scan_in_progress = false;
        s_sd.state = LoaderState::Idle;
        return false;
    }

    const char* base = s_sd.fsi.GetSDPath();
    std::snprintf(s_sd.scan_path, sizeof(s_sd.scan_path), "%sWAV", base);

    if(f_opendir(&s_sd.dir, s_sd.scan_path) != FR_OK)
    {
        std::snprintf(s_sd.scan_path, sizeof(s_sd.scan_path), "%s", base);
        if(f_opendir(&s_sd.dir, s_sd.scan_path) != FR_OK)
        {
            SdBrowser_SetStatus(sd, "DIR ERR");
            sd.scan_in_progress = false;
            s_sd.state = LoaderState::Idle;
            return false;
        }
    }

    s_sd.dir_open = true;
    return true;
}

static bool ScanStep(AppState& app)
{
    SdBrowserState& sd = app.sd;
    if(!s_sd.dir_open)
        return true;

    static constexpr uint8_t kEntriesPerTick = 4;
    FILINFO fno;
    for(uint8_t i = 0; i < kEntriesPerTick; ++i)
    {
        const FRESULT res = f_readdir(&s_sd.dir, &fno);
        if(res != FR_OK)
        {
            f_closedir(&s_sd.dir);
            s_sd.dir_open = false;
            s_sd.state = LoaderState::Idle;
            sd.scan_in_progress = false;
            SdBrowser_SetStatus(sd, "SD ERR");
            return true;
        }
        if(fno.fname[0] == 0)
        {
            f_closedir(&s_sd.dir);
            s_sd.dir_open = false;
            s_sd.state = LoaderState::Idle;
            sd.scan_in_progress = false;
            sd.scan_done = true;
            if(sd.wav_count == 0)
                SdBrowser_SetStatus(sd, "NO WAV");
            else
                SdBrowser_SetStatus(sd, "");
            SdBrowser_RebuildMenu(sd);
            return true;
        }
        if(fno.fattrib & AM_DIR)
            continue;
        if(!IsWavName(fno.fname))
            continue;
        if(sd.wav_count >= kSdMaxFiles)
        {
            f_closedir(&s_sd.dir);
            s_sd.dir_open = false;
            s_sd.state = LoaderState::Idle;
            sd.scan_in_progress = false;
            sd.scan_done = true;
            SdBrowser_SetStatus(sd, "LIST FULL");
            SdBrowser_RebuildMenu(sd);
            return true;
        }

        const uint8_t idx = sd.wav_count;
        std::snprintf(sd.names[idx], sizeof(sd.names[idx]), "%.*s",
                      (int)sizeof(sd.names[idx]) - 1,
                      fno.fname);
        if(!MakePath(sd.paths[idx], sizeof(sd.paths[idx]), s_sd.scan_path, fno.fname))
        {
            SdBrowser_SetStatus(sd, "PATH TOO LONG");
            sd.scan_in_progress = false;
            sd.scan_done = true;
            f_closedir(&s_sd.dir);
            s_sd.dir_open = false;
            s_sd.state = LoaderState::Idle;
            SdBrowser_RebuildMenu(sd);
            return true;
        }
        sd.wav_count++;
    }

    return false;
}

static bool StartLoad(AppState& app, uint16_t index)
{
    SdBrowserState& sd = app.sd;
    s_sd.state = LoaderState::Idle;
    sd.load_pending = false;
    if(!EnsureSdMounted(app))
        return false;

    if(index >= sd.wav_count)
    {
        SdBrowser_SetStatus(sd, "BAD IDX");
        sd.wav_err_count++;
        return false;
    }

    if(f_open(&s_sd.file, sd.paths[index], FA_READ | FA_OPEN_EXISTING) != FR_OK)
    {
        SdBrowser_SetStatus(sd, "OPEN ERR");
        sd.wav_err_count++;
        return false;
    }

    WavInfo info;
    if(!ParseWavHeader(s_sd.file, info))
    {
        f_close(&s_sd.file);
        SdBrowser_SetStatus(sd, "BAD WAV");
        sd.wav_err_count++;
        return false;
    }

    if(info.audio_format != 1 || info.channels != 1 || info.bits_per_sample != 16
       || info.sample_rate != 48000)
    {
        f_close(&s_sd.file);
        SdBrowser_SetStatus(sd, "UNSUPPORTED");
        sd.wav_err_count++;
        return false;
    }

    const uint32_t frames = info.data_size / 2u;
    if(frames == 0 || frames > SdSampleMaxFrames())
    {
        f_close(&s_sd.file);
        SdBrowser_SetStatus(sd, "TOO LONG");
        sd.wav_err_count++;
        return false;
    }

    if(f_lseek(&s_sd.file, info.data_offset) != FR_OK)
    {
        f_close(&s_sd.file);
        SdBrowser_SetStatus(sd, "SEEK ERR");
        sd.wav_err_count++;
        return false;
    }

    std::snprintf(sd.last_loaded_path, sizeof(sd.last_loaded_path), "%s", sd.paths[index]);

    const uint8_t current_slot = app.sd_current_slot.load(std::memory_order_acquire);
    const uint8_t next_slot = current_slot ^ 1u;
    s_sd.loading_slot = next_slot;
    s_sd.state = LoaderState::Load;
    s_sd.file_open = true;
    s_sd.data_size = info.data_size;
    s_sd.bytes_loaded = 0;
    s_sd.sample_frames = frames;
    s_sd.load_index = index;

    sd.load_in_progress = true;
    sd.load_progress = 0;
    SdBrowser_SetStatus(sd, "LOADING");

    return true;
}

static bool StartLoadPath(AppState& app, const char* path, uint8_t target_slot)
{
    SdBrowserState& sd = app.sd;
    s_sd.state = LoaderState::Idle;
    sd.load_pending = false;
    if(!EnsureSdMounted(app))
        return false;

    if(!path || path[0] == '\0')
    {
        SdBrowser_SetStatus(sd, "BAD PATH");
        sd.wav_err_count++;
        return false;
    }

    if(f_open(&s_sd.file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
    {
        SdBrowser_SetStatus(sd, "OPEN ERR");
        sd.wav_err_count++;
        return false;
    }

    WavInfo info;
    if(!ParseWavHeader(s_sd.file, info))
    {
        f_close(&s_sd.file);
        SdBrowser_SetStatus(sd, "BAD WAV");
        sd.wav_err_count++;
        return false;
    }

    if(info.audio_format != 1 || info.channels != 1 || info.bits_per_sample != 16
       || info.sample_rate != 48000)
    {
        f_close(&s_sd.file);
        SdBrowser_SetStatus(sd, "UNSUPPORTED");
        sd.wav_err_count++;
        return false;
    }

    const uint32_t frames = info.data_size / 2u;
    if(frames == 0 || frames > SdSampleMaxFrames())
    {
        f_close(&s_sd.file);
        SdBrowser_SetStatus(sd, "TOO LONG");
        sd.wav_err_count++;
        return false;
    }

    if(f_lseek(&s_sd.file, info.data_offset) != FR_OK)
    {
        f_close(&s_sd.file);
        SdBrowser_SetStatus(sd, "SEEK ERR");
        sd.wav_err_count++;
        return false;
    }

    std::snprintf(sd.last_loaded_path, sizeof(sd.last_loaded_path), "%s", path);

    s_sd.loading_slot = target_slot & 1u;
    s_sd.state = LoaderState::Load;
    s_sd.file_open = true;
    s_sd.data_size = info.data_size;
    s_sd.bytes_loaded = 0;
    s_sd.sample_frames = frames;
    s_sd.load_index = 0xFFFFu;

    sd.load_in_progress = true;
    sd.load_progress = 0;
    SdBrowser_SetStatus(sd, "LOADING");

    return true;
}

static bool LoadStep(AppState& app, uint16_t budget)
{
    SdBrowserState& sd = app.sd;
    if(!s_sd.file_open)
        return true;

    const uint32_t bytes_left = (s_sd.data_size > s_sd.bytes_loaded)
                                ? (s_sd.data_size - s_sd.bytes_loaded)
                                : 0;
    if(bytes_left == 0)
        return true;

    uint32_t bytes_to_read = bytes_left;
    if(bytes_to_read > budget)
        bytes_to_read = budget;
    bytes_to_read &= ~1u;
    if(bytes_to_read == 0)
        bytes_to_read = (bytes_left >= 2) ? 2 : bytes_left;

    UINT br = 0;
    uint8_t* dst = reinterpret_cast<uint8_t*>(SdSampleBuffer(s_sd.loading_slot));
    const FRESULT res = f_read(&s_sd.file, dst + s_sd.bytes_loaded, bytes_to_read, &br);
    if(res != FR_OK || br == 0)
    {
        f_close(&s_sd.file);
        s_sd.file_open = false;
        s_sd.state = LoaderState::Idle;
        sd.load_in_progress = false;
        SdBrowser_SetStatus(sd, "READ ERR");
        sd.wav_err_count++;
        ClearProjectRestoreState(app);
        app.ui_req_result = -1;
        return true;
    }

    s_sd.bytes_loaded += br;
    uint32_t pct = (s_sd.bytes_loaded * 100u) / s_sd.data_size;
    if(pct > 100u)
        pct = 100u;
    sd.load_progress = static_cast<uint8_t>(pct);

    if(s_sd.bytes_loaded >= s_sd.data_size)
    {
        f_close(&s_sd.file);
        s_sd.file_open = false;

        Sample& samp = app.sd_slots[s_sd.loading_slot];
        samp.pcm = SdSampleBuffer(s_sd.loading_slot);
        samp.length = s_sd.sample_frames;
        samp.sample_rate = 48000;
        samp.root_key = 60;
        samp.loop_start = 0;
        samp.loop_end = s_sd.sample_frames;
        samp.loop_enabled = false;

        SampleEdit edit = SampleEdit_Default(s_sd.sample_frames);
        const uint8_t edit_bit = static_cast<uint8_t>(1u << (s_sd.loading_slot & 1u));
        const bool is_project_restore_load = (app.project_edit_pending_mask & edit_bit) != 0u;
        if(is_project_restore_load)
        {
            edit = app.project_pending_edit[s_sd.loading_slot & 1u];
            SampleEdit_Clamp(edit, s_sd.sample_frames);
            app.project_edit_pending_mask &= static_cast<uint8_t>(~edit_bit);
        }
        app.sd_edit_slots[s_sd.loading_slot] = edit;
        if(!is_project_restore_load)
        {
            app.perform_adsr_loop_crossfade[s_sd.loading_slot & 1u] = 0.0625f;
            app.perform_adsr_loop_crossfade_shape[s_sd.loading_slot & 1u] = 0.0f;
        }
        app.sd_edit_pending = edit;
        app.sd_edit_slot.store(s_sd.loading_slot, std::memory_order_release);
        app.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
        app.sd_edit_ready.store(1, std::memory_order_release);

        const uint32_t next_gen = app.sd_published_gen.fetch_add(1, std::memory_order_acq_rel) + 1u;
        (void)next_gen;
        app.sd_published_slot.store(s_sd.loading_slot, std::memory_order_release);
        app.sd_published_ready.store(1, std::memory_order_release);
        s_sd.state = LoaderState::Idle;

        sd.load_in_progress = false;
        sd.load_progress = 100;
        sd.last_loaded_index = s_sd.load_index;
        SdBrowser_SetStatus(sd, "LOADED");
        if(app.ui_req_busy && app.ui_req_active == UiReqType::LoadProject
           && s_sd.project_restore_pending_mask != 0u)
        {
            if(StartNextProjectRestoreLoad(app))
                return false;
            ClearProjectRestoreState(app);
            app.ui_req_result = -1;
            return true;
        }
        return true;
    }

    return false;
}

static bool StartNormalize(AppState& app)
{
    const uint8_t slot = app.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const Sample& sample = app.sd_slots[slot];
    if(sample.pcm == nullptr || sample.length == 0)
    {
        SdBrowser_SetStatus(app.sd, "NO SAMPLE");
        return false;
    }

    SampleEdit edit = app.sd_edit_slots[slot];
    SampleEdit_Clamp(edit, sample.length);

    s_sd.norm_active = true;
    s_sd.norm_slot = slot;
    s_sd.norm_start = edit.start_frame;
    s_sd.norm_end = edit.end_frame;
    s_sd.norm_pos = edit.start_frame;
    s_sd.norm_peak = 0;
    return true;
}

static bool NormalizeStep(AppState& app, uint16_t budget_us)
{
    if(!s_sd.norm_active)
        return true;

    const uint8_t slot = s_sd.norm_slot;
    const Sample& sample = app.sd_slots[slot];
    if(sample.pcm == nullptr || sample.length == 0)
    {
        s_sd.norm_active = false;
        return true;
    }

    uint32_t frames_left = (s_sd.norm_end > s_sd.norm_pos)
                           ? (s_sd.norm_end - s_sd.norm_pos)
                           : 0;
    if(frames_left == 0)
        return true;

    uint32_t frames_budget = budget_us * 32u;
    if(frames_budget < 256u)
        frames_budget = 256u;
    if(frames_budget > 4096u)
        frames_budget = 4096u;
    uint32_t frames_to_do = frames_left;
    if(frames_to_do > frames_budget)
        frames_to_do = frames_budget;

    const int16_t* pcm = sample.pcm;
    uint32_t pos = s_sd.norm_pos;
    int32_t peak = s_sd.norm_peak;
    for(uint32_t i = 0; i < frames_to_do; ++i)
    {
        const int16_t v = pcm[pos + i];
        const int32_t av = (v < 0) ? -v : v;
        if(av > peak)
            peak = av;
    }
    s_sd.norm_peak = peak;
    s_sd.norm_pos = pos + frames_to_do;

    const uint32_t done = s_sd.norm_pos - s_sd.norm_start;
    const uint32_t total = s_sd.norm_end - s_sd.norm_start;
    if(total > 0)
    {
        uint32_t pct = (done * 100u) / total;
        if(pct > 100u)
            pct = 100u;
        app.sd.load_progress = static_cast<uint8_t>(pct);
    }

    if(s_sd.norm_pos >= s_sd.norm_end)
    {
        SampleEdit edit = app.sd_edit_slots[slot];
        SampleEdit_Clamp(edit, sample.length);

        const float target = 0.891f;
        if(peak <= 0)
            edit.gain = 1.0f;
        else
        {
            const float peak_f = (float)peak / 32768.0f;
            float gain = target / peak_f;
            if(gain > 8.0f)
                gain = 8.0f;
            edit.gain = gain;
        }

        app.sd_edit_slots[slot] = edit;
        app.sd_edit_pending = edit;
        app.sd_edit_slot.store(slot, std::memory_order_release);
        app.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
        app.sd_edit_ready.store(1, std::memory_order_release);
        s_sd.norm_active = false;
        SdBrowser_SetStatus(app.sd, "NORM OK");
        return true;
    }

    return false;
}

static uint32_t FindBestBoundary(const int16_t* pcm,
                                 uint32_t start,
                                 uint32_t end,
                                 uint32_t center,
                                 uint32_t window)
{
    if(end <= start + 1 || pcm == nullptr)
        return start;
    uint32_t lo = (center > window) ? (center - window) : (start + 1);
    uint32_t hi = center + window;
    if(lo < start + 1)
        lo = start + 1;
    if(hi >= end)
        hi = end - 1;

    uint32_t best = center;
    uint32_t best_score = 0xFFFFFFFFu;
    for(uint32_t i = lo; i <= hi; ++i)
    {
        const int16_t a = pcm[i - 1];
        const int16_t b = pcm[i];
        uint32_t score = (uint32_t)((a < 0 ? -a : a) + (b < 0 ? -b : b));
        if(((a ^ b) < 0))
            score >>= 1;
        if(score < best_score)
        {
            best_score = score;
            best = i;
        }
    }
    return best;
}

static bool LoopFindCurrent(AppState& app)
{
    const uint8_t slot = app.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const Sample& sample = app.sd_slots[slot];
    if(sample.pcm == nullptr || sample.length == 0)
    {
        SdBrowser_SetStatus(app.sd, "NO SAMPLE");
        return false;
    }

    SampleEdit edit = app.sd_edit_slots[slot];
    SampleEdit_Clamp(edit, sample.length);
    const uint32_t start = edit.start_frame;
    const uint32_t end = edit.end_frame;
    if(end <= start + 1)
        return false;

    const uint32_t window = 2048u;
    uint32_t ls = edit.loop_start;
    uint32_t le = edit.loop_end;
    if(ls < start || ls >= end)
        ls = start;
    if(le <= ls || le > end)
        le = end;

    ls = FindBestBoundary(sample.pcm, start, end, ls, window);
    le = FindBestBoundary(sample.pcm, start, end, le, window);
    if(le <= ls + 1)
        le = (ls + 1 < end) ? (ls + 1) : end;

    edit.loop_start = ls;
    edit.loop_end = le;
    app.sd_edit_slots[slot] = edit;
    app.sd_edit_pending = edit;
    app.sd_edit_slot.store(slot, std::memory_order_release);
    app.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
    app.sd_edit_ready.store(1, std::memory_order_release);
    SdBrowser_SetStatus(app.sd, "LOOP OK");
    return true;
}

static bool StartSave(AppState& app)
{
    SdBrowserState& sd = app.sd;
    sd.save_in_progress = false;
    sd.save_progress = 0;
    SdBrowser_SetSaveStatus(sd, "SAVING");
    SdBrowser_SetSaveName(sd, "");

    if(!EnsureSdMounted(app))
    {
        SdBrowser_SetSaveStatus(sd, "SD ERR");
        return false;
    }

    const uint8_t slot = app.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const Sample& sample = app.sd_slots[slot];
    if(sample.pcm == nullptr || sample.length == 0)
    {
        SdBrowser_SetSaveStatus(sd, "NO SAMPLE");
        return false;
    }

    SampleEdit edit = app.sd_edit_slots[slot];
    SampleEdit_Clamp(edit, sample.length);
    if(edit.end_frame <= edit.start_frame || edit.end_frame > sample.length)
    {
        SdBrowser_SetSaveStatus(sd, "BAD RANGE");
        return false;
    }

    s_sd.save_slot = slot;
    s_sd.save_start = edit.start_frame;
    s_sd.save_end = edit.end_frame;
    s_sd.save_total = edit.end_frame - edit.start_frame;
    s_sd.save_written = 0;
    s_sd.save_gain = edit.gain;

    const char* base = s_sd.fsi.GetSDPath();
    std::snprintf(s_sd.save_dir, sizeof(s_sd.save_dir), "%s", base);

    bool found = false;
    for(uint16_t i = 1; i <= 9999u; ++i)
    {
        char name[kSdNameMax];
        std::snprintf(name, sizeof(name), "REND%04u.WAV", static_cast<unsigned>(i));
        if(!MakePath(s_sd.save_path, sizeof(s_sd.save_path), s_sd.save_dir, name))
        {
            SdBrowser_SetSaveStatus(sd, "PATH LONG");
            return false;
        }

        FILINFO fno;
        const FRESULT st = f_stat(s_sd.save_path, &fno);
        if(st == FR_NO_FILE)
        {
            std::snprintf(s_sd.save_name, sizeof(s_sd.save_name), "%s", name);
            found = true;
            break;
        }
        if(st != FR_OK)
        {
            SdBrowser_SetSaveStatus(sd, "STAT ERR");
            return false;
        }
    }

    if(!found)
    {
        SdBrowser_SetSaveStatus(sd, "NO NAME");
        return false;
    }

    if(f_open(&s_sd.file, s_sd.save_path, FA_WRITE | FA_CREATE_NEW) != FR_OK)
    {
        SdBrowser_SetSaveStatus(sd, "OPEN ERR");
        return false;
    }

    s_sd.file_open = true;
    if(!WriteWavHeader(s_sd.file, s_sd.save_total))
    {
        f_close(&s_sd.file);
        s_sd.file_open = false;
        SdBrowser_SetSaveStatus(sd, "WRITE ERR");
        return false;
    }

    s_sd.save_active = true;
    sd.save_in_progress = true;
    sd.save_progress = 0;
    SdBrowser_SetSaveStatus(sd, "SAVING");
    SdBrowser_SetSaveName(sd, s_sd.save_name);
    return true;
}

static bool SaveStep(AppState& app, uint16_t budget_us)
{
    SdBrowserState& sd = app.sd;
    if(!s_sd.save_active || !s_sd.file_open)
        return true;

    const Sample& sample = app.sd_slots[s_sd.save_slot];
    if(sample.pcm == nullptr || s_sd.save_total == 0)
    {
        if(s_sd.file_open)
        {
            f_close(&s_sd.file);
            s_sd.file_open = false;
        }
        s_sd.save_active = false;
        sd.save_in_progress = false;
        sd.save_progress = 0;
        SdBrowser_SetSaveStatus(sd, "SAVE ERR");
        app.ui_req_result = -1;
        return true;
    }

    const uint32_t remaining = (s_sd.save_total > s_sd.save_written)
                               ? (s_sd.save_total - s_sd.save_written)
                               : 0;
    if(remaining == 0)
        return true;

    uint32_t max_frames = kSaveChunkFrames;
    if(budget_us > 0)
    {
        uint32_t cap = budget_us / 2u;
        if(cap < 128u)
            cap = 128u;
        if(cap > kSaveChunkFrames)
            cap = kSaveChunkFrames;
        max_frames = cap;
    }
    uint32_t frames_to_write = remaining;
    if(frames_to_write > max_frames)
        frames_to_write = max_frames;

    static int16_t s_save_buf[kSaveChunkFrames];
    const int16_t* src = sample.pcm + s_sd.save_start + s_sd.save_written;
    const float gain = s_sd.save_gain;
    const bool unity_gain = (gain > 0.9999f && gain < 1.0001f);
    const void* write_ptr = s_save_buf;
    if(unity_gain)
    {
        write_ptr = src;
    }
    else
    {
        for(uint32_t i = 0; i < frames_to_write; ++i)
        {
            float x = static_cast<float>(src[i]) * gain;
            int32_t v = (x >= 0.0f) ? static_cast<int32_t>(x + 0.5f)
                                    : static_cast<int32_t>(x - 0.5f);
            if(v > 32767)
                v = 32767;
            else if(v < -32768)
                v = -32768;
            s_save_buf[i] = static_cast<int16_t>(v);
        }
    }

    UINT bw = 0;
    const uint32_t bytes = frames_to_write * 2u;
    const FRESULT res = f_write(&s_sd.file, write_ptr, bytes, &bw);
    if(res != FR_OK || bw != bytes)
    {
        f_close(&s_sd.file);
        s_sd.file_open = false;
        s_sd.save_active = false;
        sd.save_in_progress = false;
        sd.save_progress = 0;
        SdBrowser_SetSaveStatus(sd, "SAVE ERR");
        app.ui_req_result = -1;
        return true;
    }

    s_sd.save_written += frames_to_write;
    uint32_t pct = (s_sd.save_written * 100u) / s_sd.save_total;
    if(pct > 100u)
        pct = 100u;
    sd.save_progress = static_cast<uint8_t>(pct);

    if(s_sd.save_written >= s_sd.save_total)
    {
        f_sync(&s_sd.file);
        f_close(&s_sd.file);
        s_sd.file_open = false;
        s_sd.save_active = false;
        sd.save_in_progress = false;
        sd.save_progress = 100;
        SdBrowser_SetSaveStatus(sd, "SAVED");
        SdBrowser_SetSaveName(sd, s_sd.save_name);
        return true;
    }

    return false;
}

static bool SaveProject(AppState& app, const Params& params)
{
    const uint8_t project_slot = RequestedProjectSlot(app);
    SetProjectSlotStatus(app, project_slot, "SAVING");

    if(!EnsureSdMounted(app))
    {
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    ProjectManifestV7 manifest{};
    const PerformParamsTargets& targets = params.TargetsForUI();
    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
    {
        const Sample& sample = app.sd_slots[slot];
        const char* path = app.engine_sample_path[slot];
        if(sample.pcm == nullptr || sample.length == 0 || !path || path[0] == '\0')
            continue;

        const uint8_t bit = static_cast<uint8_t>(1u << slot);
        manifest.sample_present_mask |= bit;
        std::snprintf(manifest.wav_path[slot], sizeof(manifest.wav_path[slot]), "%s", path);

        SampleEdit edit = app.sd_edit_slots[slot];
        SampleEdit_Clamp(edit, sample.length);
        manifest.edit[slot] = edit;
        manifest.engine_tune_semitones[slot] = ClampProjectTune(app.engine_tune_semitones[slot]);
        manifest.perform_keyzone_lo_note[slot] = app.perform_keyzone_lo_note[slot];
        manifest.perform_keyzone_hi_note[slot] = app.perform_keyzone_hi_note[slot];
        manifest.perform_adsr_row[slot] = ClampProjectAdsrRow(app.perform_adsr_row[slot]);
        manifest.engine_play_mode[slot] = ClampProjectPlayMode(app.engine_play_mode[slot]);
        manifest.perform_adsr_loop_attack[slot] = app.perform_adsr_loop_attack[slot];
        manifest.perform_adsr_loop_decay[slot] = app.perform_adsr_loop_decay[slot];
        manifest.perform_adsr_loop_sustain[slot] = app.perform_adsr_loop_sustain[slot];
        manifest.perform_adsr_loop_release[slot] = app.perform_adsr_loop_release[slot];
        manifest.perform_adsr_loop_crossfade[slot] = app.perform_adsr_loop_crossfade[slot];
        manifest.perform_adsr_loop_crossfade_shape[slot] = app.perform_adsr_loop_crossfade_shape[slot];
        manifest.perform_adsr_env_a_x[slot] = app.perform_adsr_env_a_x[slot];
        manifest.perform_adsr_env_d_x[slot] = app.perform_adsr_env_d_x[slot];
        manifest.perform_adsr_env_r_x[slot] = app.perform_adsr_env_r_x[slot];
        manifest.perform_adsr_env_s_level[slot] = app.perform_adsr_env_s_level[slot];
        manifest.engine_gain_db[slot] = app.engine_gain_db[slot];
        manifest.engine_drive_mode[slot] = app.engine_drive_mode[slot];
        manifest.engine_filter_cutoff_hz[slot] = targets.engine_filter_cutoff_hz[slot];
        manifest.engine_filter_resonance[slot] = targets.engine_filter_resonance[slot];
    }

    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
    {
        manifest.engine_layer_master_level[slot]
            = ClampProjectFloat(targets.engine_layer_master_level[slot], 0.0f, 2.0f);
        if((manifest.sample_present_mask & static_cast<uint8_t>(1u << slot)) == 0u)
            manifest.engine_tune_semitones[slot] = ClampProjectTune(app.engine_tune_semitones[slot]);
        manifest.perform_keyzone_lo_note[slot] = app.perform_keyzone_lo_note[slot];
        manifest.perform_keyzone_hi_note[slot] = app.perform_keyzone_hi_note[slot];
        ClampProjectKeyzoneRange(manifest.perform_keyzone_lo_note[slot],
                                 manifest.perform_keyzone_hi_note[slot]);
        manifest.perform_adsr_row[slot] = ClampProjectAdsrRow(app.perform_adsr_row[slot]);
        manifest.engine_play_mode[slot] = ClampProjectPlayMode(app.engine_play_mode[slot]);
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

    if(manifest.sample_present_mask == 0u)
    {
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    manifest.seq_running = app.seq_running ? 1 : 0;
    manifest.plock_apply_enabled = app.plock_apply_enabled ? 1 : 0;
    manifest.lfo_wave = app.lfo_wave.load(std::memory_order_relaxed);
    manifest.seq_bpm = app.seq_bpm;
    manifest.macro_ui = app.macro_ui;
    manifest.macro_sel = app.macro_sel.load(std::memory_order_relaxed) & 1u;
    for(size_t i = 0; i < kMaxModRoutes; ++i)
        manifest.mod_routes[i] = app.mod_routes_ui[i];
    manifest.mod_route_selected = app.mod_route_selected;

    char tmp_path[kProjectPathMax];
    char prj_path[kProjectPathMax];
    const char* base = s_sd.fsi.GetSDPath();
    if(!MakeProjectSlotPath(tmp_path, sizeof(tmp_path), base, project_slot, "TMP")
       || !MakeProjectSlotPath(prj_path, sizeof(prj_path), base, project_slot, "AKPRJ"))
    {
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    if(f_open(&s_sd.file, tmp_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    UINT bw = 0;
    const FRESULT wr = f_write(&s_sd.file, &manifest, sizeof(manifest), &bw);
    f_close(&s_sd.file);
    if(wr != FR_OK || bw != sizeof(manifest))
    {
        f_unlink(tmp_path);
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    f_unlink(prj_path);
    if(f_rename(tmp_path, prj_path) != FR_OK)
    {
        f_unlink(tmp_path);
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

    SetProjectSlotStatus(app, project_slot, "SAVED");
    return true;
}

static bool DeleteWavAtIndex(AppState& app, uint16_t idx)
{
    SdBrowserState& sd = app.sd;
    if(!EnsureSdMounted(app))
    {
        SdBrowser_SetStatus(sd, "DEL ERR");
        return false;
    }
    if(idx >= sd.wav_count)
    {
        SdBrowser_SetStatus(sd, "DEL ERR");
        return false;
    }
    if(sd.paths[idx][0] == '\0')
    {
        SdBrowser_SetStatus(sd, "DEL ERR");
        return false;
    }

    const FRESULT fr = f_unlink(sd.paths[idx]);
    if(fr == FR_OK)
    {
        SdBrowser_SetStatus(sd, "DELETED");
        return true;
    }
    SdBrowser_SetStatus(sd, "DEL ERR");
    return false;
}

static bool LoadProject(AppState& app, Params& params)
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

    ProjectManifestV7 manifest{};
    const FSIZE_t manifest_size = f_size(&s_sd.file);
    UINT br = 0;
    FRESULT rd = FR_INT_ERR;
    if(manifest_size == sizeof(ProjectManifestV7))
    {
        rd = f_read(&s_sd.file, &manifest, sizeof(manifest), &br);
    }
    else if(manifest_size == sizeof(ProjectManifestV6))
    {
        ProjectManifestV6 legacy_v6{};
        rd = f_read(&s_sd.file, &legacy_v6, sizeof(legacy_v6), &br);
        if(rd == FR_OK && br == sizeof(legacy_v6) && ProjectManifestValid(legacy_v6))
            ProjectManifestUpgrade(manifest, legacy_v6);
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
            ProjectManifestUpgrade(manifest, legacy_v6);
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
            ProjectManifestUpgrade(manifest, legacy_v6);
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
            ProjectManifestUpgrade(manifest, legacy_v6);
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
            ProjectManifestUpgrade(manifest, legacy_v6);
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
            ProjectManifestUpgrade(manifest, legacy_v6);
        }
        else
            rd = FR_INVALID_OBJECT;
    }
    f_close(&s_sd.file);
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
        manifest.wav_path[slot][sizeof(manifest.wav_path[slot]) - 1] = '\0';

    const bool manifest_ok
        = (manifest_size == sizeof(ProjectManifestV7) && rd == FR_OK && br == sizeof(manifest)
           && ProjectManifestValid(manifest))
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
    if(!manifest_ok)
    {
        SetProjectSlotStatus(app, project_slot, "ERR");
        return false;
    }

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

    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        app.engine_tune_semitones[slot] = ClampProjectTune(manifest.engine_tune_semitones[slot]);
        app.perform_keyzone_lo_note[slot] = manifest.perform_keyzone_lo_note[slot];
        app.perform_keyzone_hi_note[slot] = manifest.perform_keyzone_hi_note[slot];
        ClampProjectKeyzoneRange(app.perform_keyzone_lo_note[slot],
                                 app.perform_keyzone_hi_note[slot]);
        app.perform_adsr_row[slot] = ClampProjectAdsrRow(manifest.perform_adsr_row[slot]);
        app.engine_play_mode[slot] = ClampProjectPlayMode(manifest.engine_play_mode[slot]);
        app.perform_adsr_loop_attack[slot] = manifest.perform_adsr_loop_attack[slot];
        if(app.perform_adsr_loop_attack[slot] < 1u)
            app.perform_adsr_loop_attack[slot] = 1u;
        if(app.perform_adsr_loop_attack[slot] > 1000u)
            app.perform_adsr_loop_attack[slot] = 1000u;
        app.perform_adsr_loop_decay[slot] = manifest.perform_adsr_loop_decay[slot];
        if(app.perform_adsr_loop_decay[slot] < 1u)
            app.perform_adsr_loop_decay[slot] = 1u;
        if(app.perform_adsr_loop_decay[slot] > 100u)
            app.perform_adsr_loop_decay[slot] = 100u;
        app.perform_adsr_loop_sustain[slot] = manifest.perform_adsr_loop_sustain[slot];
        if(app.perform_adsr_loop_sustain[slot] > 100u)
            app.perform_adsr_loop_sustain[slot] = 100u;
        app.perform_adsr_loop_release[slot] = manifest.perform_adsr_loop_release[slot];
        if(app.perform_adsr_loop_release[slot] < 1u)
            app.perform_adsr_loop_release[slot] = 1u;
        if(app.perform_adsr_loop_release[slot] > 1000u)
            app.perform_adsr_loop_release[slot] = 1000u;
        app.perform_adsr_loop_crossfade[slot]
            = ClampProjectFloat(manifest.perform_adsr_loop_crossfade[slot], 0.0f, 0.5f);
        app.perform_adsr_loop_crossfade_shape[slot]
            = ClampProjectFloat(manifest.perform_adsr_loop_crossfade_shape[slot], 0.0f, 1.0f);
        app.perform_adsr_env_a_x[slot] = manifest.perform_adsr_env_a_x[slot];
        app.perform_adsr_env_d_x[slot] = manifest.perform_adsr_env_d_x[slot];
        app.perform_adsr_env_r_x[slot] = manifest.perform_adsr_env_r_x[slot];
        app.perform_adsr_env_s_level[slot] = manifest.perform_adsr_env_s_level[slot];
        ClampProjectAdsrGraph(app.perform_adsr_env_a_x[slot],
                              app.perform_adsr_env_d_x[slot],
                              app.perform_adsr_env_r_x[slot],
                              app.perform_adsr_env_s_level[slot]);
        app.engine_gain_db[slot] = ClampProjectEngineGainDb(manifest.engine_gain_db[slot]);
        app.engine_drive_mode[slot] = ClampProjectDriveMode(manifest.engine_drive_mode[slot]);
    }
    PublishProjectPerformParams(params,
                                app,
                                manifest.engine_layer_master_level,
                                manifest.engine_filter_cutoff_hz,
                                manifest.engine_filter_resonance);
    SyncProjectProcessVolumeUiState(app, manifest.engine_layer_master_level);

    ClearProjectRestoreState(app);
    for(uint8_t slot = 0; slot < kProjectSampleLayerCount; ++slot)
    {
        if(!ProjectManifestHasLayer(manifest, slot))
            continue;

        const uint8_t bit = static_cast<uint8_t>(1u << slot);
        app.project_pending_edit[slot] = manifest.edit[slot];
        app.project_edit_pending_mask |= bit;
        s_sd.project_restore_pending_mask |= bit;
        std::snprintf(s_sd.project_restore_path[slot],
                      sizeof(s_sd.project_restore_path[slot]),
                      "%s",
                      manifest.wav_path[slot]);
        std::snprintf(app.engine_sample_path[slot],
                      sizeof(app.engine_sample_path[slot]),
                      "%s",
                      manifest.wav_path[slot]);
        ExtractBaseName(manifest.wav_path[slot],
                        app.engine_sample_name[slot],
                        sizeof(app.engine_sample_name[slot]));
    }

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

static void StepFakeWork(AppState& app, uint16_t budget_us)
{
    const uint32_t total = app.ui_req_work_units_total;
    uint32_t done = app.ui_req_work_units_done;
    if(done >= total)
        return;

    uint32_t units_left = total - done;
    uint32_t units_to_do = units_left;
    const uint32_t max_units = (budget_us == 0) ? 1u : (uint32_t)budget_us;
    if(units_to_do > max_units)
        units_to_do = max_units;

    for(uint32_t i = 0; i < units_to_do; ++i)
        s_work_sink += (i + done);

    done += units_to_do;
    app.ui_req_work_units_done = done;
    uint32_t pct = (done * 100u) / total;
    if(pct > 100u)
        pct = 100u;
    app.ui_req_progress = static_cast<uint8_t>(pct);
}

void UiWorker_Tick(AppState& app, Params& params, uint32_t now_ms, uint16_t budget_us)
{
    (void)now_ms;
    const uint8_t prev_progress = app.ui_req_progress;
    const bool prev_busy = app.ui_req_busy;
    const UiReqType prev_active = app.ui_req_active;

    if(app.sd.load_pending)
    {
        const bool block_pending = app.ui_req_busy
                                   && (app.ui_req_active == UiReqType::SaveRenderedWavCurrent
                                       || app.ui_req_active == UiReqType::LoadProject);
        if(!block_pending)
        {
            const uint16_t idx = app.sd.load_pending_index;
            app.sd.load_pending = false;

            if(app.ui_req_busy)
            {
                if(app.ui_req_active == UiReqType::ScanSdWavs)
                    CancelScan(app);
                if(app.ui_req_active == UiReqType::LoadWavIndex)
                    CancelLoad(app);
                if(app.ui_req_active == UiReqType::NormalizeCurrent)
                {
                    s_sd.norm_active = false;
                    app.sd.load_in_progress = false;
                    app.sd.load_progress = 0;
                }
            }

            app.ui_req_busy = true;
            app.ui_req_active = UiReqType::LoadWavIndex;
            app.ui_req_progress = 0;
            app.ui_req_result = 0;
            app.ui_req_arg0 = idx;
            app.ui_req_work_units_done = 0;
            app.ui_req_work_units_total = 0;
            if(!StartLoad(app, idx))
            {
                app.ui_req_result = -1;
                FinishRequest(app);
            }
        }
    }

    if(!app.ui_req_busy)
    {
        UiReq r{};
        if(!UiReq_Pop(app, r))
            return;

        app.ui_req_busy = true;
        app.ui_req_active = r.type;
        app.ui_req_progress = 0;
        app.ui_req_result = 0;
        app.ui_req_arg0 = r.a;
        app.ui_req_work_units_done = 0;
        app.ui_req_work_units_total = 0;

        switch(r.type)
        {
            case UiReqType::ScanSdWavs:
                if(!StartScan(app))
                {
                    app.ui_req_result = -1;
                    FinishRequest(app);
                }
                break;
            case UiReqType::LoadWavIndex:
                if(!StartLoad(app, r.a))
                {
                    app.sd.load_in_progress = false;
                    app.sd.load_progress = 0;
                    app.ui_req_result = -1;
                    FinishRequest(app);
                }
                break;
            case UiReqType::DeleteWavIndex:
                if(!DeleteWavAtIndex(app, r.a))
                    app.ui_req_result = -1;
                FinishRequest(app);
                break;
            case UiReqType::NormalizeCurrent:
                if(!StartNormalize(app))
                {
                    app.ui_req_result = -1;
                    FinishRequest(app);
                }
                else
                {
                    app.sd.load_in_progress = true;
                    app.sd.load_progress = 0;
                }
                break;
            case UiReqType::LoopFindCurrent:
                if(!LoopFindCurrent(app))
                {
                    app.ui_req_result = -1;
                }
                FinishRequest(app);
                break;
            case UiReqType::SaveProject:
                if(!SaveProject(app, params))
                    app.ui_req_result = -1;
                FinishRequest(app);
                break;
            case UiReqType::LoadProject:
                if(!LoadProject(app, params))
                {
                    app.ui_req_result = -1;
                    FinishRequest(app);
                }
                break;
            case UiReqType::SaveRenderedWavCurrent:
                if(!StartSave(app))
                {
                    app.ui_req_result = -1;
                    FinishRequest(app);
                }
                break;
            case UiReqType::RebuildCache:
            case UiReqType::LoadSample:
            case UiReqType::SavePreset:
                app.ui_req_work_units_total = (r.type == UiReqType::RebuildCache) ? 2000u
                                             : (r.type == UiReqType::LoadSample)   ? 800u
                                             : 200u;
                break;
            case UiReqType::None:
            default:
                FinishRequest(app);
                break;
        }
    }

    if(!app.ui_req_busy)
        return;

    bool done = false;
    switch(app.ui_req_active)
    {
        case UiReqType::ScanSdWavs:
            done = ScanStep(app);
            if(done)
            {
                app.ui_req_progress = 100;
                FinishRequest(app);
            }
            break;
        case UiReqType::LoadWavIndex:
            done = LoadStep(app, budget_us * 2u);
            app.ui_req_progress = app.sd.load_progress;
            if(done)
                FinishRequest(app);
            break;
        case UiReqType::NormalizeCurrent:
            done = NormalizeStep(app, budget_us);
            app.ui_req_progress = app.sd.load_progress;
            if(done)
            {
                app.sd.load_in_progress = false;
                FinishRequest(app);
            }
            break;
        case UiReqType::LoopFindCurrent:
            FinishRequest(app);
            done = true;
            break;
        case UiReqType::LoadProject:
            done = LoadStep(app, budget_us * 2u);
            app.ui_req_progress = app.sd.load_progress;
            if(done)
            {
                const uint8_t project_slot = RequestedProjectSlot(app);
                if(app.ui_req_result < 0)
                    SetProjectSlotStatus(app, project_slot, "ERR");
                else
                    SetProjectSlotStatus(app, project_slot, "LOADED");
                FinishRequest(app);
            }
            break;
        case UiReqType::SaveRenderedWavCurrent:
            done = SaveStep(app, budget_us);
            app.ui_req_progress = app.sd.save_progress;
            if(done)
                FinishRequest(app);
            break;
        case UiReqType::RebuildCache:
        case UiReqType::LoadSample:
        case UiReqType::SavePreset:
            StepFakeWork(app, budget_us);
            if(app.ui_req_work_units_done >= app.ui_req_work_units_total)
                FinishRequest(app);
            break;
        default:
            FinishRequest(app);
            break;
    }

    if(app.ui_req_progress != prev_progress || app.ui_req_busy != prev_busy
       || app.ui_req_active != prev_active)
        app.ui_dirty = true;
}
