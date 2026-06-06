#include "ui_worker_sample_load.h"
#include "ui_worker_internal.h"

#include "app_state_engine.h"
#include "app_state_shared.h"
#include "app_state_ui.h"
#include "app_state_worker.h"
#include "sd_browser_state.h"
#include "sd_sample_pool.h"
#include "sample_bake.h"
#include "sample_edit.h"
#include "ui_requests.h"

#include "fatfs.h"
#include "ff.h"

#include <cstdio>
#include <cstring>

using namespace daisy;

extern SdWorkerState s_sd;

static bool FailLoadStart(SdBrowserState& sd, const char* status)
{
    SdBrowser_SetStatus(sd, status);
    sd.wav_err_count++;
    return false;
}

static bool StartLoadFromPathInternal(SdBrowserState& sd,
                                      AppSharedState& shared,
                                      const char* path,
                                      LoadTarget load_target,
                                      uint8_t loading_slot,
                                      uint16_t load_index)
{
    s_sd.state = LoaderState::Idle;
    sd.load_pending = false;
    if(!EnsureSdMountedInternal(sd))
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
    // Per-slot cap: slot 0 plays from RAM_D2 (smaller) so its limit is lower.
    // SdManage edits go to the SDRAM manage buffer (full kSdSampleMaxFrames).
    const uint32_t max_frames = (load_target == LoadTarget::SdManage)
                                    ? SdSampleMaxFrames(1)
                                    : SdSampleMaxFrames(loading_slot & 1u);
    if(frames == 0 || frames > max_frames)
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
    s_sd.load_target = load_target;
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
    SdWavLoad_SetBusy(shared, sd, true);

    return true;
}

bool StartLoadInternal(SdBrowserState& sd, AppSharedState& shared, uint16_t index)
{
    s_sd.state = LoaderState::Idle;
    sd.load_pending = false;
    if(index >= sd.wav_count)
        return FailLoadStart(sd, "BAD IDX");

    const uint8_t current_slot = shared.sample.publish.sd_current_slot.load(std::memory_order_acquire);
    const uint8_t next_slot = current_slot ^ 1u;
    return StartLoadFromPathInternal(sd, shared, sd.paths[index], LoadTarget::LiveSlot, next_slot, index);
}

bool StartLoadSdManage(SdBrowserState& sd, AppSharedState& shared, uint16_t index)
{
    s_sd.state = LoaderState::Idle;
    sd.load_pending = false;
    if(index >= sd.wav_count)
        return FailLoadStart(sd, "BAD IDX");

    return StartLoadFromPathInternal(sd, shared, sd.paths[index], LoadTarget::SdManage, 0u, index);
}

bool StartLoadPath(AppUiState& ui, AppSharedState& shared, const char* path, uint8_t target_slot)
{
    s_sd.state = LoaderState::Idle;
    ui.sd.load_pending = false;
    if(!path || path[0] == '\0')
        return FailLoadStart(ui.sd, "BAD PATH");
    return StartLoadFromPathInternal(ui.sd, shared, path, LoadTarget::LiveSlot, target_slot, 0xFFFFu);
}

void ClearProjectRestoreStateInternal(ProjectRestoreState& project_restore)
{
    s_sd.project_restore_pending_mask = 0;
    project_restore.project_edit_pending_mask = 0;
    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
        s_sd.project_restore_path[slot][0] = '\0';
}

void ClearProjectRestoreState(AppWorkerState& worker)
{
    ClearProjectRestoreStateInternal(worker.project_restore);
}

static bool StartNextProjectRestoreLoadInternal(SdBrowserState& sd,
                                                AppEngineState& engine,
                                                ProjectRestoreState& project_restore,
                                                AppSharedState& shared)
{
    for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
    {
        const uint8_t bit = static_cast<uint8_t>(1u << slot);
        if((s_sd.project_restore_pending_mask & bit) == 0u)
            continue;
        if(s_sd.project_restore_path[slot][0] == '\0')
        {
            s_sd.project_restore_pending_mask &= static_cast<uint8_t>(~bit);
            project_restore.project_edit_pending_mask &= static_cast<uint8_t>(~bit);
            continue;
        }

        engine.perform_nav.perform_layer = slot;
        if(!StartLoadFromPathInternal(sd,
                                      shared,
                                      s_sd.project_restore_path[slot],
                                      LoadTarget::LiveSlot,
                                      slot,
                                      0xFFFFu))
            return false;

        s_sd.project_restore_pending_mask &= static_cast<uint8_t>(~bit);
        return true;
    }
    return false;
}

bool StartNextProjectRestoreLoad(AppUiState& ui,
                                 AppEngineState& engine,
                                 AppWorkerState& worker,
                                 AppSharedState& shared)
{
    return StartNextProjectRestoreLoadInternal(ui.sd, engine, worker.project_restore, shared);
}

bool LoadStepInternal(SdBrowserState& sd,
                      AppWorkerState& worker,
                      AppEngineState& engine,
                      AppSharedState& shared,
                      ProjectRestoreState& project_restore,
                      uint16_t budget)
{
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
    int16_t* sample_dst = (s_sd.load_target == LoadTarget::SdManage)
                              ? SdManageBuffer()
                              : SdSampleLoadBuffer(s_sd.loading_slot);
    uint8_t* dst = reinterpret_cast<uint8_t*>(sample_dst);
    const FRESULT res = f_read(&s_sd.file, dst + s_sd.bytes_loaded, bytes_to_read, &br);
    if(res != FR_OK || br == 0)
    {
        f_close(&s_sd.file);
        s_sd.file_open = false;
        s_sd.state = LoaderState::Idle;
        sd.load_in_progress = false;
        SdWavLoad_SetBusy(shared, sd, false);
        SdBrowser_SetStatus(sd, "READ ERR");
        sd.wav_err_count++;
        ClearProjectRestoreStateInternal(project_restore);
        worker.ui_req_result = -1;
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

        SampleEdit edit = SampleEdit_Default(s_sd.sample_frames);
        if(s_sd.load_target == LoadTarget::SdManage)
        {
            Sample& samp = shared.sd_manage.sample;
            samp.pcm = SdManageBuffer();
            samp.length = s_sd.sample_frames;
            samp.sample_rate = 48000;
            samp.root_key = 60;
            samp.loop_start = 0;
            samp.loop_end = s_sd.sample_frames;
            samp.loop_enabled = false;
            shared.sd_manage.edit = edit;
        }
        else
        {
            // Slot 0 staged in SDRAM; copy it into the RAM_D2 playback buffer
            // before publishing the pcm pointer the audio thread will read.
            SdSampleLoadCommit(s_sd.loading_slot, s_sd.sample_frames);

            Sample& samp = shared.sample.publish.sd_slots[s_sd.loading_slot];
            samp.pcm = SdSampleBuffer(s_sd.loading_slot);
            samp.length = s_sd.sample_frames;
            samp.sample_rate = 48000;
            samp.root_key = 60;
            samp.loop_start = 0;
            samp.loop_end = s_sd.sample_frames;
            samp.loop_enabled = false;

            const uint8_t edit_bit = static_cast<uint8_t>(1u << (s_sd.loading_slot & 1u));
            const bool is_project_restore_load = (project_restore.project_edit_pending_mask & edit_bit) != 0u;
            if(is_project_restore_load)
            {
                edit = project_restore.project_pending_edit[s_sd.loading_slot & 1u];
                SampleEdit_Clamp(edit, s_sd.sample_frames);
                project_restore.project_edit_pending_mask &= static_cast<uint8_t>(~edit_bit);
            }
            shared.sample.edit.sd_edit_slots[s_sd.loading_slot] = edit;
            if(!is_project_restore_load)
            {
                engine.adsr.perform_adsr_loop_crossfade[s_sd.loading_slot & 1u]
                    = DefaultSeamCrossfadeAmount(edit.start_frame, edit.end_frame);
                engine.adsr.perform_adsr_loop_crossfade_shape[s_sd.loading_slot & 1u] = 0.0f;
            }

            // Loop seam handling is done by the runtime live crossfade (see
            // voice_engine_render_voice.cpp). The sample plays from the raw SD buffer
            // (samp.pcm set above) and the seam length/curve come from the params; no
            // PCM bake at load. The default seam length was set above.
            const uint8_t bake_layer = s_sd.loading_slot & 1u;
            engine.adsr.perform_adsr_loop_seam_baked[bake_layer] = false;
            shared.sample.publish.sd_layer_seam_baked[s_sd.loading_slot].store(
                0u, std::memory_order_release);

            shared.sample.edit.sd_edit_pending = edit;
            shared.sample.edit.sd_edit_slot.store(s_sd.loading_slot, std::memory_order_release);
            shared.sample.edit.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
            shared.sample.edit.sd_edit_ready.store(1, std::memory_order_release);

            const uint32_t next_gen = shared.sample.publish.sd_published_gen.fetch_add(1, std::memory_order_acq_rel) + 1u;
            (void)next_gen;
            shared.sample.publish.sd_published_slot.store(s_sd.loading_slot, std::memory_order_release);
            shared.sample.publish.sd_published_ready.store(1, std::memory_order_release);
        }
        s_sd.state = LoaderState::Idle;

        sd.load_in_progress = false;
        SdWavLoad_SetBusy(shared, sd, false);
        sd.load_progress = 100;
        sd.last_loaded_index = s_sd.load_index;
        SdBrowser_SetStatus(sd, "LOADED");
        if(s_sd.load_target == LoadTarget::LiveSlot
           && worker.ui_req_busy
           && worker.ui_req_active == UiReqType::LoadProject
           && s_sd.project_restore_pending_mask != 0u)
        {
            if(StartNextProjectRestoreLoadInternal(sd, engine, project_restore, shared))
                return false;
            ClearProjectRestoreStateInternal(project_restore);
            worker.ui_req_result = -1;
            return true;
        }
        return true;
    }

    return false;
}
