#include "ui_worker_sample_ops.h"
#include "ui_worker_internal.h"
#include "ui_worker_project_internal.h"

#include "app_state_shared.h"
#include "app_state_ui.h"
#include "app_state_worker.h"
#include "sd_browser_state.h"
#include "sample_edit.h"

#include "fatfs.h"
#include "ff.h"
#include "mem_regions.h"

#include <cstdio>
#include <cstring>

extern SdWorkerState s_sd;

static constexpr uint32_t kSaveChunkFrames = 512;

static void ReplaceRenamedPathInProjectManifests(const char* old_path, const char* new_path);

namespace
{
char ToAsciiUpper(char c)
{
    if(c >= 'a' && c <= 'z')
        return static_cast<char>(c - ('a' - 'A'));
    return c;
}

bool EqualsIgnoreCase(const char* a, const char* b)
{
    if(!a || !b)
        return false;

    while(*a != '\0' && *b != '\0')
    {
        if(ToAsciiUpper(*a) != ToAsciiUpper(*b))
            return false;
        ++a;
        ++b;
    }
    return (*a == '\0') && (*b == '\0');
}

const char* BasenameStart(const char* path)
{
    if(!path)
        return nullptr;

    const char* base = path;
    for(const char* p = path; *p != '\0'; ++p)
    {
        if(*p == '/' || *p == '\\')
            base = p + 1;
    }
    return base;
}

bool SanitizeSampleRenameStem(const char* input, char* out, size_t out_n)
{
    if(!out || out_n == 0u)
        return false;

    out[0] = '\0';
    if(!input || input[0] == '\0')
        return false;

    if(!BuildSampleDisplayName(input, out, out_n))
        return false;
    const size_t len = std::strlen(out);
    if(len >= 2u && out[len - 2u] == '@' && SampleStyleFromCode(out[len - 1u]) != SampleStyle::None)
        out[len - 2u] = '\0';
    return out[0] != '\0';
}

bool RenameSampleAtIndexToPath(SdBrowserState& sd, uint16_t idx, const char* new_path)
{
    if(idx >= sd.wav_count || !new_path || new_path[0] == '\0')
    {
        SdBrowser_SetStatus(sd, "REN ERR");
        return false;
    }

    const char* old_path = sd.paths[idx];
    if(!old_path || old_path[0] == '\0')
    {
        SdBrowser_SetStatus(sd, "REN ERR");
        return false;
    }

    if(std::strcmp(new_path, old_path) == 0)
    {
        SdBrowser_SetStatus(sd, "RENAMED");
        return true;
    }

    FILINFO info{};
    const FRESULT stat_res = f_stat(new_path, &info);
    if(stat_res == FR_OK && !EqualsIgnoreCase(new_path, old_path))
    {
        SdBrowser_SetStatus(sd, "NAME EXISTS");
        return false;
    }
    if(stat_res != FR_OK && stat_res != FR_NO_FILE && stat_res != FR_NO_PATH)
    {
        SdBrowser_SetStatus(sd, "REN ERR");
        return false;
    }

    char old_path_copy[kSdPathMax];
    const int old_copy_written = std::snprintf(old_path_copy, sizeof(old_path_copy), "%s", old_path);
    if(old_copy_written < 0 || old_copy_written >= static_cast<int>(sizeof(old_path_copy)))
    {
        SdBrowser_SetStatus(sd, "REN ERR");
        return false;
    }

    if(f_rename(old_path_copy, new_path) != FR_OK)
    {
        SdBrowser_SetStatus(sd, "REN ERR");
        return false;
    }

    const char* new_name = BasenameStart(new_path);
    if(!new_name || !SdBrowser_SetWavFileAtIndex(sd, idx, new_name, new_path))
    {
        SdBrowser_SetStatus(sd, "REN ERR");
        return false;
    }

    SdBrowser_RebuildMenu(sd);
    ReplaceRenamedPathInProjectManifests(old_path_copy, new_path);
    SdBrowser_SetStatus(sd, "RENAMED");
    return true;
}

bool BuildReplaceTempPath(const char* original_path, char* out_path, size_t out_path_n)
{
    if(!original_path || !out_path || out_path_n == 0u)
        return false;

    const char* base = BasenameStart(original_path);
    if(!base)
        return false;

    const size_t dir_len = static_cast<size_t>(base - original_path);
    if(dir_len + std::strlen("__SDTRIM_TMP.WAV") + 1u > out_path_n)
        return false;

    if(dir_len > 0u)
        std::memcpy(out_path, original_path, dir_len);
    out_path[dir_len] = '\0';
    std::snprintf(out_path + dir_len, out_path_n - dir_len, "%s", "__SDTRIM_TMP.WAV");
    return true;
}
} // namespace

// SDRAM-resident write scratch: SDMMC DMA can't reach the DTCM main-loop stack
// where `manifest` lives, so f_write straight from it silently writes 0 bytes.
// (Same fix as WriteProjectManifestFile / ReadProjectManifestFromFile.)
ADSR2_SECTION(".sdram_bss") static uint8_t s_sampleops_manifest_io[sizeof(ProjectManifestV11)];

static bool WriteProjectManifestToSlot(uint8_t project_slot, const ProjectManifestV11& manifest)
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

    std::memcpy(s_sampleops_manifest_io, &manifest, sizeof(manifest));
    UINT bw = 0;
    const FRESULT wr = f_write(&s_sd.file, s_sampleops_manifest_io, sizeof(manifest), &bw);
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

static void ReplaceRenamedPathInProjectManifests(const char* old_path, const char* new_path)
{
    if(!old_path || !new_path || old_path[0] == '\0' || new_path[0] == '\0')
        return;

    const char* base = s_sd.fsi.GetSDPath();
    for(uint8_t slot = 0; slot < kProjectSlotCount; ++slot)
    {
        char prj_path[kProjectPathMax];
        if(!MakeProjectSlotPath(prj_path, sizeof(prj_path), base, slot, "AKPRJ"))
            continue;
        if(f_open(&s_sd.file, prj_path, FA_READ) != FR_OK)
            continue;

        ProjectManifestV11 manifest{};
        if(!ReadProjectManifestFromFile(manifest))
            continue;

        bool changed = false;
        for(uint8_t layer = 0; layer < kProjectSampleLayerCount; ++layer)
        {
            if(std::strcmp(manifest.wav_path[layer], old_path) == 0)
            {
                std::snprintf(manifest.wav_path[layer], sizeof(manifest.wav_path[layer]), "%s", new_path);
                changed = true;
            }
        }
        if(!changed)
            continue;

        WriteProjectManifestToSlot(slot, manifest);
    }
}

static void CloseWorkerFileIfOpen()
{
    if(!s_sd.file_open)
        return;
    f_close(&s_sd.file);
    s_sd.file_open = false;
}

static bool SelectSaveDirectory(SdBrowserState& sd)
{
    const char* base = s_sd.fsi.GetSDPath();
    std::snprintf(s_sd.save_dir, sizeof(s_sd.save_dir), "%s", base);
    DIR dir;
    if(f_opendir(&dir, s_sd.save_dir) == FR_OK)
    {
        f_closedir(&dir);
        return true;
    }

    SdBrowser_SetSaveStatus(sd, "DIR ERR");
    return false;
}

static void FailActiveSave(SdBrowserState& sd,
                           AppWorkerState* worker,
                           const char* stage,
                           FRESULT res,
                           UINT bw,
                           uint32_t requested_bytes)
{
    std::printf("SAVE ERR stage=%s path=%s name=%s res=%d bw=%u req=%lu written=%lu total=%lu\n",
                stage ? stage : "?",
                s_sd.save_path[0] != '\0' ? s_sd.save_path : "(none)",
                s_sd.save_name[0] != '\0' ? s_sd.save_name : "(none)",
                static_cast<int>(res),
                static_cast<unsigned>(bw),
                static_cast<unsigned long>(requested_bytes),
                static_cast<unsigned long>(s_sd.save_written),
                static_cast<unsigned long>(s_sd.save_total));

    CloseWorkerFileIfOpen();

    if(s_sd.save_path[0] != '\0')
    {
        const FRESULT unlink_res = f_unlink(s_sd.save_path);
        std::printf("SAVE ERR cleanup path=%s unlink_res=%d\n",
                    s_sd.save_path,
                    static_cast<int>(unlink_res));
    }

    s_sd.save_active = false;
    sd.save_in_progress = false;
    sd.save_progress = 0;
    SdBrowser_SetSaveStatus(sd, "SAVE ERR");
    if(worker)
        worker->ui_req_result = -1;
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

bool StartSave(AppUiState& ui,
               AppSharedState& shared,
               SampleSaveSource save_source,
               const char* save_stem,
               const char* replace_path)
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
    const Sample* sample = nullptr;
    SampleEdit edit{};
    switch(save_source)
    {
        case SampleSaveSource::Recording:
            sample = &shared.recording.rec_sample;
            edit = shared.recording.rec_edit;
            break;
        case SampleSaveSource::SdManage:
            sample = &shared.sd_manage.sample;
            edit = shared.sd_manage.edit;
            break;
        case SampleSaveSource::LiveSlot:
        default:
            sample = &shared.sample.publish.sd_slots[slot];
            edit = shared.sample.edit.sd_edit_slots[slot];
            break;
    }
    if(!sample || sample->pcm == nullptr || sample->length == 0)
    {
        SdBrowser_SetSaveStatus(sd, "NO SAMPLE");
        return false;
    }

    SampleEdit_Clamp(edit, sample->length);
    if(edit.end_frame <= edit.start_frame || edit.end_frame > sample->length)
    {
        SdBrowser_SetSaveStatus(sd, "BAD RANGE");
        return false;
    }

    s_sd.save_slot = slot;
    s_sd.save_pcm = sample->pcm;
    s_sd.save_start = edit.start_frame;
    s_sd.save_end = edit.end_frame;
    s_sd.save_total = edit.end_frame - edit.start_frame;
    s_sd.save_written = 0;
    s_sd.save_gain = edit.gain;
    s_sd.save_finalize_mode = (replace_path && replace_path[0] != '\0') ? SaveFinalizeMode::ReplaceExisting
                                                                        : SaveFinalizeMode::AddNew;
    s_sd.save_replace_path[0] = '\0';

    if(!SelectSaveDirectory(sd))
        return false;

    bool found = false;
    if(s_sd.save_finalize_mode == SaveFinalizeMode::ReplaceExisting)
    {
        if(!BuildReplaceTempPath(replace_path, s_sd.save_path, sizeof(s_sd.save_path)))
        {
            SdBrowser_SetSaveStatus(sd, "PATH LONG");
            return false;
        }

        std::snprintf(s_sd.save_replace_path, sizeof(s_sd.save_replace_path), "%s", replace_path);
        const char* original_name = BasenameStart(replace_path);
        if(!original_name || original_name[0] == '\0')
        {
            SdBrowser_SetSaveStatus(sd, "PATH ERR");
            return false;
        }
        std::snprintf(s_sd.save_name, sizeof(s_sd.save_name), "%s", original_name);
        f_unlink(s_sd.save_path);
        found = true;
    }
    else if(save_stem && save_stem[0] != '\0')
    {
        char name[kSdNameMax];
        std::snprintf(name, sizeof(name), "%s.WAV", save_stem);
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
        }
        else if(st == FR_OK)
        {
            SdBrowser_SetSaveStatus(sd, "NAME EXISTS");
            return false;
        }
        else
        {
            SdBrowser_SetSaveStatus(sd, "STAT ERR");
            return false;
        }
    }
    else
    {
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
    FRESULT header_res = FR_OK;
    UINT header_bw = 0;
    uint32_t header_requested = 0;
    if(!WriteWavHeader(s_sd.file, s_sd.save_total, &header_res, &header_bw, &header_requested))
    {
        FailActiveSave(sd, nullptr, "header", header_res, header_bw, header_requested);
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
    (void)shared;
    if(!s_sd.save_active || !s_sd.file_open)
        return true;

    if(s_sd.save_pcm == nullptr || s_sd.save_total == 0)
    {
        FailActiveSave(sd, &worker, "invalid-state", FR_INT_ERR, 0, 0);
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
    const int16_t* src = s_sd.save_pcm + s_sd.save_start + s_sd.save_written;
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
        FailActiveSave(sd, &worker, (res != FR_OK) ? "pcm-write" : "pcm-short-write", res, bw, bytes);
        return true;
    }

    s_sd.save_written += frames_to_write;
    uint32_t pct = (s_sd.save_written * 100u) / s_sd.save_total;
    if(pct > 100u)
        pct = 100u;
    sd.save_progress = static_cast<uint8_t>(pct);

    if(s_sd.save_written >= s_sd.save_total)
    {
        const FRESULT sync_res = f_sync(&s_sd.file);
        if(sync_res != FR_OK)
        {
            FailActiveSave(sd, &worker, "sync", sync_res, 0, 0);
            return true;
        }

        CloseWorkerFileIfOpen();
        s_sd.save_active = false;
        sd.save_in_progress = false;
        sd.save_progress = 100;
        if(s_sd.save_finalize_mode == SaveFinalizeMode::ReplaceExisting)
        {
            const FRESULT unlink_res = f_unlink(s_sd.save_replace_path);
            if(unlink_res != FR_OK && unlink_res != FR_NO_FILE)
            {
                FailActiveSave(sd, &worker, "replace-unlink", unlink_res, 0, 0);
                return true;
            }

            const FRESULT rename_res = f_rename(s_sd.save_path, s_sd.save_replace_path);
            if(rename_res != FR_OK)
            {
                FailActiveSave(sd, &worker, "replace-rename", rename_res, 0, 0);
                return true;
            }

            SdBrowser_SetSaveStatus(sd, "REPLACED");
            SdBrowser_SetSaveName(sd, s_sd.save_name);
            return true;
        }

        SdBrowser_SetSaveStatus(sd, "SAVED");
        SdBrowser_SetSaveName(sd, s_sd.save_name);
        SdBrowser_AddWavFile(sd, s_sd.save_name, s_sd.save_path);
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
        SdBrowser_RemoveWavAtIndex(sd, idx);
        SdBrowser_SetStatus(sd, "DELETED");
        return true;
    }
    SdBrowser_SetStatus(sd, "DEL ERR");
    return false;
}

bool RenameWavAtIndex(SdBrowserState& sd, uint16_t idx, const char* new_stem)
{
    if(!EnsureSdMountedInternal(sd) || idx >= sd.wav_count || !new_stem || new_stem[0] == '\0')
    {
        SdBrowser_SetStatus(sd, "REN ERR");
        return false;
    }

    char sanitized_stem[kSdNameMax];
    if(!SanitizeSampleRenameStem(new_stem, sanitized_stem, sizeof(sanitized_stem)))
    {
        SdBrowser_SetStatus(sd, "REN ERR");
        return false;
    }

    char new_path[kSdPathMax];
    if(!BuildStyledSamplePathFromStem(sd.paths[idx],
                                      sanitized_stem,
                                      sd.styles[idx],
                                      new_path,
                                      sizeof(new_path)))
    {
        SdBrowser_SetStatus(sd, "REN ERR");
        return false;
    }

    return RenameSampleAtIndexToPath(sd, idx, new_path);
}

bool UpdateWavStyleAtIndex(SdBrowserState& sd, uint16_t idx, SampleStyle desired_style)
{
    if(!EnsureSdMountedInternal(sd) || idx >= sd.wav_count)
    {
        SdBrowser_SetStatus(sd, "REN ERR");
        return false;
    }
    if(sd.paths[idx][0] == '\0')
    {
        SdBrowser_SetStatus(sd, "REN ERR");
        return false;
    }

    char new_path[kSdPathMax];
    if(!BuildStyledSamplePath(sd.paths[idx], desired_style, new_path, sizeof(new_path)))
    {
        SdBrowser_SetStatus(sd, "REN ERR");
        return false;
    }

    return RenameSampleAtIndexToPath(sd, idx, new_path);
}
