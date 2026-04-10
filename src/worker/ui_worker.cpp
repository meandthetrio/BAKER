#include "ui_worker.h"
#include "ui_worker_internal.h"

#include "app_state.h"
#include "sd_browser_state.h"
#include "sd_sample_pool.h"
#include "ui_requests.h"
#include "sample_edit.h"
#include "params.h"

#include "fatfs.h"
#include "ff.h"
#include "per/sdmmc.h"

#include <cstdio>

using namespace daisy;

static volatile uint32_t s_work_sink = 0;

SdWorkerState s_sd;

// Shared SD worker lifecycle helpers
bool EnsureSdMounted(AppState& app)
{
    SdBrowserState& sd = app.ui.sd;
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
    SdBrowserState& sd = app.ui.sd;
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
    SdBrowserState& sd = app.ui.sd;
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
    app.worker.ui_req_busy = false;
    app.worker.ui_req_active = UiReqType::None;
    app.worker.ui_req_progress = 100;
    app.worker.ui_req_done_count++;
    s_sd.state = LoaderState::Idle;
    ClearProjectRestoreState(app);
}

// Scan flow
static bool StartScan(AppState& app)
{
    SdBrowserState& sd = app.ui.sd;
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
    SdBrowserState& sd = app.ui.sd;
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

// Load and project-restore flow
static bool FailLoadStart(SdBrowserState& sd, const char* status)
{
    SdBrowser_SetStatus(sd, status);
    sd.wav_err_count++;
    return false;
}

static bool StartLoadFromPath(AppState& app, const char* path, uint8_t loading_slot, uint16_t load_index)
{
    SdBrowserState& sd = app.ui.sd;
    s_sd.state = LoaderState::Idle;
    sd.load_pending = false;
    if(!EnsureSdMounted(app))
        return false;

    if(f_open(&s_sd.file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return FailLoadStart(sd, "OPEN ERR");

    WavInfo info;
    if(!ParseWavHeader(s_sd.file, info))
    {
        f_close(&s_sd.file);
        return FailLoadStart(sd, "BAD WAV");
    }

    if(info.audio_format != 1 || info.channels != 1 || info.bits_per_sample != 16
       || info.sample_rate != 48000)
    {
        f_close(&s_sd.file);
        return FailLoadStart(sd, "UNSUPPORTED");
    }

    const uint32_t frames = info.data_size / 2u;
    if(frames == 0 || frames > SdSampleMaxFrames())
    {
        f_close(&s_sd.file);
        return FailLoadStart(sd, "TOO LONG");
    }

    if(f_lseek(&s_sd.file, info.data_offset) != FR_OK)
    {
        f_close(&s_sd.file);
        return FailLoadStart(sd, "SEEK ERR");
    }

    std::snprintf(sd.last_loaded_path, sizeof(sd.last_loaded_path), "%s", path);
    s_sd.loading_slot = loading_slot & 1u;
    s_sd.state = LoaderState::Load;
    s_sd.file_open = true;
    s_sd.data_size = info.data_size;
    s_sd.bytes_loaded = 0;
    s_sd.sample_frames = frames;
    s_sd.load_index = load_index;

    sd.load_in_progress = true;
    sd.load_progress = 0;
    SdBrowser_SetStatus(sd, "LOADING");

    return true;
}

static bool StartLoad(AppState& app, uint16_t index)
{
    SdBrowserState& sd = app.ui.sd;
    s_sd.state = LoaderState::Idle;
    sd.load_pending = false;
    if(index >= sd.wav_count)
        return FailLoadStart(sd, "BAD IDX");

    const uint8_t current_slot = app.shared.sd_current_slot.load(std::memory_order_acquire);
    const uint8_t next_slot = current_slot ^ 1u;
    return StartLoadFromPath(app, sd.paths[index], next_slot, index);
}

bool StartLoadPath(AppState& app, const char* path, uint8_t target_slot)
{
    s_sd.state = LoaderState::Idle;
    app.ui.sd.load_pending = false;
    if(!path || path[0] == '\0')
        return FailLoadStart(app.ui.sd, "BAD PATH");
    return StartLoadFromPath(app, path, target_slot, 0xFFFFu);
}

void ClearProjectRestoreState(AppState& app)
{
    s_sd.project_restore_pending_mask = 0;
    app.project.project_edit_pending_mask = 0;
    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
        s_sd.project_restore_path[slot][0] = '\0';
}

bool StartNextProjectRestoreLoad(AppState& app)
{
    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
    {
        const uint8_t bit = static_cast<uint8_t>(1u << slot);
        if((s_sd.project_restore_pending_mask & bit) == 0u)
            continue;
        if(s_sd.project_restore_path[slot][0] == '\0')
        {
            s_sd.project_restore_pending_mask &= static_cast<uint8_t>(~bit);
            app.project.project_edit_pending_mask &= static_cast<uint8_t>(~bit);
            continue;
        }

        app.engine.perform_layer = slot;
        if(!StartLoadPath(app, s_sd.project_restore_path[slot], slot))
            return false;

        s_sd.project_restore_pending_mask &= static_cast<uint8_t>(~bit);
        return true;
    }
    return false;
}

static bool LoadStep(AppState& app, uint16_t budget)
{
    SdBrowserState& sd = app.ui.sd;
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
        app.worker.ui_req_result = -1;
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

        Sample& samp = app.shared.sd_slots[s_sd.loading_slot];
        samp.pcm = SdSampleBuffer(s_sd.loading_slot);
        samp.length = s_sd.sample_frames;
        samp.sample_rate = 48000;
        samp.root_key = 60;
        samp.loop_start = 0;
        samp.loop_end = s_sd.sample_frames;
        samp.loop_enabled = false;

        SampleEdit edit = SampleEdit_Default(s_sd.sample_frames);
        const uint8_t edit_bit = static_cast<uint8_t>(1u << (s_sd.loading_slot & 1u));
        const bool is_project_restore_load = (app.project.project_edit_pending_mask & edit_bit) != 0u;
        if(is_project_restore_load)
        {
            edit = app.project.project_pending_edit[s_sd.loading_slot & 1u];
            SampleEdit_Clamp(edit, s_sd.sample_frames);
            app.project.project_edit_pending_mask &= static_cast<uint8_t>(~edit_bit);
        }
        app.shared.sd_edit_slots[s_sd.loading_slot] = edit;
        if(!is_project_restore_load)
        {
            app.engine.perform_adsr_loop_crossfade[s_sd.loading_slot & 1u] = 0.0625f;
            app.engine.perform_adsr_loop_crossfade_shape[s_sd.loading_slot & 1u] = 0.0f;
        }
        app.shared.sd_edit_pending = edit;
        app.shared.sd_edit_slot.store(s_sd.loading_slot, std::memory_order_release);
        app.shared.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
        app.shared.sd_edit_ready.store(1, std::memory_order_release);

        const uint32_t next_gen = app.shared.sd_published_gen.fetch_add(1, std::memory_order_acq_rel) + 1u;
        (void)next_gen;
        app.shared.sd_published_slot.store(s_sd.loading_slot, std::memory_order_release);
        app.shared.sd_published_ready.store(1, std::memory_order_release);
        s_sd.state = LoaderState::Idle;

        sd.load_in_progress = false;
        sd.load_progress = 100;
        sd.last_loaded_index = s_sd.load_index;
        SdBrowser_SetStatus(sd, "LOADED");
        if(app.worker.ui_req_busy && app.worker.ui_req_active == UiReqType::LoadProject
           && s_sd.project_restore_pending_mask != 0u)
        {
            if(StartNextProjectRestoreLoad(app))
                return false;
            ClearProjectRestoreState(app);
            app.worker.ui_req_result = -1;
            return true;
        }
        return true;
    }

    return false;
}

// Sample analysis / edit helpers
static bool StartNormalize(AppState& app)
{
    const uint8_t slot = app.shared.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const Sample& sample = app.shared.sd_slots[slot];
    if(sample.pcm == nullptr || sample.length == 0)
    {
        SdBrowser_SetStatus(app.ui.sd, "NO SAMPLE");
        return false;
    }

    SampleEdit edit = app.shared.sd_edit_slots[slot];
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
    const Sample& sample = app.shared.sd_slots[slot];
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
        app.ui.sd.load_progress = static_cast<uint8_t>(pct);
    }

    if(s_sd.norm_pos >= s_sd.norm_end)
    {
        SampleEdit edit = app.shared.sd_edit_slots[slot];
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

        app.shared.sd_edit_slots[slot] = edit;
        app.shared.sd_edit_pending = edit;
        app.shared.sd_edit_slot.store(slot, std::memory_order_release);
        app.shared.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
        app.shared.sd_edit_ready.store(1, std::memory_order_release);
        s_sd.norm_active = false;
        SdBrowser_SetStatus(app.ui.sd, "NORM OK");
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
    const uint8_t slot = app.shared.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const Sample& sample = app.shared.sd_slots[slot];
    if(sample.pcm == nullptr || sample.length == 0)
    {
        SdBrowser_SetStatus(app.ui.sd, "NO SAMPLE");
        return false;
    }

    SampleEdit edit = app.shared.sd_edit_slots[slot];
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
    app.shared.sd_edit_slots[slot] = edit;
    app.shared.sd_edit_pending = edit;
    app.shared.sd_edit_slot.store(slot, std::memory_order_release);
    app.shared.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
    app.shared.sd_edit_ready.store(1, std::memory_order_release);
    SdBrowser_SetStatus(app.ui.sd, "LOOP OK");
    return true;
}

// Rendered WAV save helpers
static constexpr uint32_t kSaveChunkFrames = 2048;

static bool StartSave(AppState& app)
{
    SdBrowserState& sd = app.ui.sd;
    sd.save_in_progress = false;
    sd.save_progress = 0;
    SdBrowser_SetSaveStatus(sd, "SAVING");
    SdBrowser_SetSaveName(sd, "");

    if(!EnsureSdMounted(app))
    {
        SdBrowser_SetSaveStatus(sd, "SD ERR");
        return false;
    }

    const uint8_t slot = app.shared.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const Sample& sample = app.shared.sd_slots[slot];
    if(sample.pcm == nullptr || sample.length == 0)
    {
        SdBrowser_SetSaveStatus(sd, "NO SAMPLE");
        return false;
    }

    SampleEdit edit = app.shared.sd_edit_slots[slot];
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
    SdBrowserState& sd = app.ui.sd;
    if(!s_sd.save_active || !s_sd.file_open)
        return true;

    const Sample& sample = app.shared.sd_slots[s_sd.save_slot];
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
        app.worker.ui_req_result = -1;
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
        app.worker.ui_req_result = -1;
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

static bool DeleteWavAtIndex(AppState& app, uint16_t idx)
{
    SdBrowserState& sd = app.ui.sd;
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

// Request-local placeholder work
static void StepFakeWork(AppState& app, uint16_t budget_us)
{
    const uint32_t total = app.worker.ui_req_work_units_total;
    uint32_t done = app.worker.ui_req_work_units_done;
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
    app.worker.ui_req_work_units_done = done;
    uint32_t pct = (done * 100u) / total;
    if(pct > 100u)
        pct = 100u;
    app.worker.ui_req_progress = static_cast<uint8_t>(pct);
}

// UiWorker_Tick helpers
static void BeginUiRequest(AppState& app, UiReqType type, uint16_t arg0)
{
    app.worker.ui_req_busy = true;
    app.worker.ui_req_active = type;
    app.worker.ui_req_progress = 0;
    app.worker.ui_req_result = 0;
    app.worker.ui_req_arg0 = arg0;
    app.worker.ui_req_work_units_done = 0;
    app.worker.ui_req_work_units_total = 0;
}

static void FailAndFinishUiRequest(AppState& app)
{
    app.worker.ui_req_result = -1;
    FinishRequest(app);
}

static bool PendingLoadBlockedByActiveRequest(const AppState& app)
{
    return app.worker.ui_req_busy
           && (app.worker.ui_req_active == UiReqType::SaveRenderedWavCurrent
               || app.worker.ui_req_active == UiReqType::LoadProject);
}

static void CancelForPendingLoad(AppState& app)
{
    if(!app.worker.ui_req_busy)
        return;

    if(app.worker.ui_req_active == UiReqType::ScanSdWavs)
        CancelScan(app);
    if(app.worker.ui_req_active == UiReqType::LoadWavIndex)
        CancelLoad(app);
    if(app.worker.ui_req_active == UiReqType::NormalizeCurrent)
    {
        s_sd.norm_active = false;
        app.ui.sd.load_in_progress = false;
        app.ui.sd.load_progress = 0;
    }
}

static void MaybeHandlePendingLoad(AppState& app)
{
    if(!app.ui.sd.load_pending || PendingLoadBlockedByActiveRequest(app))
        return;

    const uint16_t idx = app.ui.sd.load_pending_index;
    app.ui.sd.load_pending = false;
    CancelForPendingLoad(app);
    BeginUiRequest(app, UiReqType::LoadWavIndex, idx);
    if(!StartLoad(app, idx))
        FailAndFinishUiRequest(app);
}

static void StartQueuedUiRequest(AppState& app, Params& params, const UiReq& req)
{
    BeginUiRequest(app, req.type, req.a);

    switch(req.type)
    {
        case UiReqType::ScanSdWavs:
            if(!StartScan(app))
                FailAndFinishUiRequest(app);
            break;
        case UiReqType::LoadWavIndex:
            if(!StartLoad(app, req.a))
            {
                app.ui.sd.load_in_progress = false;
                app.ui.sd.load_progress = 0;
                FailAndFinishUiRequest(app);
            }
            break;
        case UiReqType::DeleteWavIndex:
            if(!DeleteWavAtIndex(app, req.a))
                app.worker.ui_req_result = -1;
            FinishRequest(app);
            break;
        case UiReqType::NormalizeCurrent:
            if(!StartNormalize(app))
            {
                FailAndFinishUiRequest(app);
            }
            else
            {
                app.ui.sd.load_in_progress = true;
                app.ui.sd.load_progress = 0;
            }
            break;
        case UiReqType::LoopFindCurrent:
            if(!LoopFindCurrent(app))
                app.worker.ui_req_result = -1;
            FinishRequest(app);
            break;
        case UiReqType::SaveProject:
            if(!SaveProject(app, params))
                app.worker.ui_req_result = -1;
            FinishRequest(app);
            break;
        case UiReqType::LoadProject:
            if(!LoadProject(app, params))
                FailAndFinishUiRequest(app);
            break;
        case UiReqType::SaveRenderedWavCurrent:
            if(!StartSave(app))
                FailAndFinishUiRequest(app);
            break;
        case UiReqType::RebuildCache:
        case UiReqType::LoadSample:
        case UiReqType::SavePreset:
            app.worker.ui_req_work_units_total = (req.type == UiReqType::RebuildCache) ? 2000u
                                                : (req.type == UiReqType::LoadSample)   ? 800u
                                                                                        : 200u;
            break;
        case UiReqType::None:
        default:
            FinishRequest(app);
            break;
    }
}

static void MaybeStartNextUiRequest(AppState& app, Params& params)
{
    if(app.worker.ui_req_busy)
        return;

    UiReq req{};
    if(!UiReq_Pop(app, req))
        return;
    StartQueuedUiRequest(app, params, req);
}

static void FinalizeLoadProjectRequest(AppState& app)
{
    const uint8_t project_slot = RequestedProjectSlot(app);
    if(app.worker.ui_req_result < 0)
        SetProjectSlotStatus(app, project_slot, "ERR");
    else
        SetProjectSlotStatus(app, project_slot, "LOADED");
    FinishRequest(app);
}

static void StepActiveUiRequest(AppState& app, Params& params, uint16_t budget_us)
{
    bool done = false;
    switch(app.worker.ui_req_active)
    {
        case UiReqType::ScanSdWavs:
            done = ScanStep(app);
            if(done)
            {
                app.worker.ui_req_progress = 100;
                FinishRequest(app);
            }
            break;
        case UiReqType::LoadWavIndex:
            done = LoadStep(app, budget_us * 2u);
            app.worker.ui_req_progress = app.ui.sd.load_progress;
            if(done)
                FinishRequest(app);
            break;
        case UiReqType::NormalizeCurrent:
            done = NormalizeStep(app, budget_us);
            app.worker.ui_req_progress = app.ui.sd.load_progress;
            if(done)
            {
                app.ui.sd.load_in_progress = false;
                FinishRequest(app);
            }
            break;
        case UiReqType::LoopFindCurrent:
            FinishRequest(app);
            break;
        case UiReqType::LoadProject:
            done = LoadStep(app, budget_us * 2u);
            app.worker.ui_req_progress = app.ui.sd.load_progress;
            if(done)
                FinalizeLoadProjectRequest(app);
            break;
        case UiReqType::SaveRenderedWavCurrent:
            done = SaveStep(app, budget_us);
            app.worker.ui_req_progress = app.ui.sd.save_progress;
            if(done)
                FinishRequest(app);
            break;
        case UiReqType::RebuildCache:
        case UiReqType::LoadSample:
        case UiReqType::SavePreset:
            StepFakeWork(app, budget_us);
            if(app.worker.ui_req_work_units_done >= app.worker.ui_req_work_units_total)
                FinishRequest(app);
            break;
        default:
            FinishRequest(app);
            break;
    }

    (void)params;
}

static void MarkUiWorkerStateDirtyIfNeeded(AppState& app,
                                           uint8_t prev_progress,
                                           bool prev_busy,
                                           UiReqType prev_active)
{
    if(app.worker.ui_req_progress != prev_progress || app.worker.ui_req_busy != prev_busy
       || app.worker.ui_req_active != prev_active)
        app.ui.ui_dirty = true;
}

// Top-level worker tick
void UiWorker_Tick(AppState& app, Params& params, uint32_t now_ms, uint16_t budget_us)
{
    (void)now_ms;
    const uint8_t prev_progress = app.worker.ui_req_progress;
    const bool prev_busy = app.worker.ui_req_busy;
    const UiReqType prev_active = app.worker.ui_req_active;

    MaybeHandlePendingLoad(app);
    MaybeStartNextUiRequest(app, params);

    if(!app.worker.ui_req_busy)
        return;

    StepActiveUiRequest(app, params, budget_us);
    MarkUiWorkerStateDirtyIfNeeded(app, prev_progress, prev_busy, prev_active);
}
