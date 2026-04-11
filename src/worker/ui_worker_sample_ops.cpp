#include "ui_worker_sample_ops.h"
#include "ui_worker_internal.h"

#include "app_state_shared.h"
#include "app_state_ui.h"
#include "app_state_worker.h"
#include "sd_browser_state.h"
#include "sample_edit.h"

#include "fatfs.h"
#include "ff.h"

#include <cstdio>

extern SdWorkerState s_sd;

static constexpr uint32_t kSaveChunkFrames = 2048;

static void CloseWorkerFileIfOpen()
{
    if(!s_sd.file_open)
        return;
    f_close(&s_sd.file);
    s_sd.file_open = false;
}

bool StartNormalize(AppUiState& ui, AppSharedState& shared)
{
    const uint8_t slot = shared.sample.publish.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const Sample& sample = shared.sample.publish.sd_slots[slot];
    if(sample.pcm == nullptr || sample.length == 0)
    {
        SdBrowser_SetStatus(ui.sd, "NO SAMPLE");
        return false;
    }

    SampleEdit edit = shared.sample.edit.sd_edit_slots[slot];
    SampleEdit_Clamp(edit, sample.length);

    s_sd.norm_active = true;
    s_sd.norm_slot = slot;
    s_sd.norm_start = edit.start_frame;
    s_sd.norm_end = edit.end_frame;
    s_sd.norm_pos = edit.start_frame;
    s_sd.norm_peak = 0;
    return true;
}

bool NormalizeStep(AppUiState& ui, AppSharedState& shared, uint16_t budget_us)
{
    if(!s_sd.norm_active)
        return true;

    const uint8_t slot = s_sd.norm_slot;
    const Sample& sample = shared.sample.publish.sd_slots[slot];
    if(sample.pcm == nullptr || sample.length == 0)
    {
        s_sd.norm_active = false;
        return true;
    }

    uint32_t frames_left = (s_sd.norm_end > s_sd.norm_pos) ? (s_sd.norm_end - s_sd.norm_pos) : 0;
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
        ui.sd.load_progress = static_cast<uint8_t>(pct);
    }

    if(s_sd.norm_pos >= s_sd.norm_end)
    {
        SampleEdit edit = shared.sample.edit.sd_edit_slots[slot];
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

        shared.sample.edit.sd_edit_slots[slot] = edit;
        shared.sample.edit.sd_edit_pending = edit;
        shared.sample.edit.sd_edit_slot.store(slot, std::memory_order_release);
        shared.sample.edit.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
        shared.sample.edit.sd_edit_ready.store(1, std::memory_order_release);
        s_sd.norm_active = false;
        SdBrowser_SetStatus(ui.sd, "NORM OK");
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

bool LoopFindCurrent(AppUiState& ui, AppSharedState& shared)
{
    const uint8_t slot = shared.sample.publish.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const Sample& sample = shared.sample.publish.sd_slots[slot];
    if(sample.pcm == nullptr || sample.length == 0)
    {
        SdBrowser_SetStatus(ui.sd, "NO SAMPLE");
        return false;
    }

    SampleEdit edit = shared.sample.edit.sd_edit_slots[slot];
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
    shared.sample.edit.sd_edit_slots[slot] = edit;
    shared.sample.edit.sd_edit_pending = edit;
    shared.sample.edit.sd_edit_slot.store(slot, std::memory_order_release);
    shared.sample.edit.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
    shared.sample.edit.sd_edit_ready.store(1, std::memory_order_release);
    SdBrowser_SetStatus(ui.sd, "LOOP OK");
    return true;
}

bool StartSave(AppUiState& ui, AppSharedState& shared)
{
    SdBrowserState& sd = ui.sd;
    sd.save_in_progress = false;
    sd.save_progress = 0;
    SdBrowser_SetSaveStatus(sd, "SAVING");
    SdBrowser_SetSaveName(sd, "");

    if(!EnsureSdMountedInternal(sd))
    {
        SdBrowser_SetSaveStatus(sd, "SD ERR");
        return false;
    }

    const uint8_t slot = shared.sample.publish.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const Sample& sample = shared.sample.publish.sd_slots[slot];
    if(sample.pcm == nullptr || sample.length == 0)
    {
        SdBrowser_SetSaveStatus(sd, "NO SAMPLE");
        return false;
    }

    SampleEdit edit = shared.sample.edit.sd_edit_slots[slot];
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
        CloseWorkerFileIfOpen();
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

bool SaveStep(SdBrowserState& sd, AppSharedState& shared, AppWorkerState& worker, uint16_t budget_us)
{
    if(!s_sd.save_active || !s_sd.file_open)
        return true;

    const Sample& sample = shared.sample.publish.sd_slots[s_sd.save_slot];
    if(sample.pcm == nullptr || s_sd.save_total == 0)
    {
        CloseWorkerFileIfOpen();
        s_sd.save_active = false;
        sd.save_in_progress = false;
        sd.save_progress = 0;
        SdBrowser_SetSaveStatus(sd, "SAVE ERR");
        worker.ui_req_result = -1;
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
        CloseWorkerFileIfOpen();
        s_sd.save_active = false;
        sd.save_in_progress = false;
        sd.save_progress = 0;
        SdBrowser_SetSaveStatus(sd, "SAVE ERR");
        worker.ui_req_result = -1;
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
        CloseWorkerFileIfOpen();
        s_sd.save_active = false;
        sd.save_in_progress = false;
        sd.save_progress = 100;
        SdBrowser_SetSaveStatus(sd, "SAVED");
        SdBrowser_SetSaveName(sd, s_sd.save_name);
        return true;
    }

    return false;
}

bool DeleteWavAtIndex(SdBrowserState& sd, uint16_t idx)
{
    if(!EnsureSdMountedInternal(sd))
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
