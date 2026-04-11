#include "ui_worker.h"
#include "ui_worker_internal.h"
#include "ui_worker_sample_load.h"
#include "ui_worker_sample_ops.h"

#include "app_state_engine.h"
#include "app_state_project.h"
#include "app_state_shared.h"
#include "app_state_ui.h"
#include "app_state_worker.h"
#include "sd_browser_state.h"
#include "sd_sample_pool.h"
#include "ui_requests.h"
#include "params.h"

#include "fatfs.h"
#include "ff.h"
#include "per/sdmmc.h"

#include <cstdio>

using namespace daisy;

static volatile uint32_t s_work_sink = 0;

SdWorkerState s_sd;

// --- SD mount and cancel hooks (shared with worker leaf modules)
bool EnsureSdMountedInternal(SdBrowserState& sd)
{
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

bool EnsureSdMounted(AppUiState& ui)
{
    return EnsureSdMountedInternal(ui.sd);
}

static void CancelScan(SdBrowserState& sd)
{
    if(s_sd.dir_open)
    {
        f_closedir(&s_sd.dir);
        s_sd.dir_open = false;
    }
    s_sd.state = LoaderState::Idle;
    sd.scan_in_progress = false;
    sd.scan_done = true;
}

static void CancelLoad(AppUiState& ui, AppWorkerState& worker)
{
    SdBrowserState& sd = ui.sd;
    if(s_sd.file_open)
    {
        f_close(&s_sd.file);
        s_sd.file_open = false;
    }
    s_sd.state = LoaderState::Idle;
    sd.load_in_progress = false;
    sd.load_progress = 0;
    ClearProjectRestoreState(worker);
}

// --- SD scan
static bool StartScan(SdBrowserState& sd)
{
    SdBrowser_ClearList(sd);
    sd.scan_in_progress = true;
    sd.scan_done = false;
    SdBrowser_SetStatus(sd, "SCANNING");
    sd.load_progress = 0;
    sd.load_in_progress = false;
    s_sd.state = LoaderState::Scan;

    if(!EnsureSdMountedInternal(sd))
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

static bool ScanStep(SdBrowserState& sd)
{
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

// --- Request-local placeholder work
static void StepFakeWork(AppWorkerState& worker, uint16_t budget_us)
{
    const uint32_t total = worker.ui_req_work_units_total;
    uint32_t done = worker.ui_req_work_units_done;
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
    worker.ui_req_work_units_done = done;
    uint32_t pct = (done * 100u) / total;
    if(pct > 100u)
        pct = 100u;
    worker.ui_req_progress = static_cast<uint8_t>(pct);
}

// --- UiWorker request queue: lifecycle, stepping, and tick entry
static void FinishRequest(AppWorkerState& worker, ProjectRestoreState& project_restore)
{
    worker.ui_req_busy = false;
    worker.ui_req_active = UiReqType::None;
    worker.ui_req_progress = 100;
    worker.ui_req_done_count++;
    s_sd.state = LoaderState::Idle;
    ClearProjectRestoreStateInternal(project_restore);
}

static void BeginUiRequest(AppWorkerState& worker, UiReqType type, uint16_t arg0)
{
    worker.ui_req_busy = true;
    worker.ui_req_active = type;
    worker.ui_req_progress = 0;
    worker.ui_req_result = 0;
    worker.ui_req_arg0 = arg0;
    worker.ui_req_work_units_done = 0;
    worker.ui_req_work_units_total = 0;
}

static void FailAndFinishUiRequest(AppWorkerState& worker, ProjectRestoreState& project_restore)
{
    worker.ui_req_result = -1;
    FinishRequest(worker, project_restore);
}

static bool PendingLoadBlockedByActiveRequest(const AppWorkerState& worker)
{
    return worker.ui_req_busy
           && (worker.ui_req_active == UiReqType::SaveRenderedWavCurrent
               || worker.ui_req_active == UiReqType::LoadProject);
}

static void CancelForPendingLoad(AppUiState& ui, AppWorkerState& worker)
{
    if(!worker.ui_req_busy)
        return;

    if(worker.ui_req_active == UiReqType::ScanSdWavs)
        CancelScan(ui.sd);
    if(worker.ui_req_active == UiReqType::LoadWavIndex)
        CancelLoad(ui, worker);
    if(worker.ui_req_active == UiReqType::NormalizeCurrent)
    {
        s_sd.norm_active = false;
        ui.sd.load_in_progress = false;
        ui.sd.load_progress = 0;
    }
}

static void MaybeHandlePendingLoad(AppUiState& ui, AppWorkerState& worker, AppSharedState& shared)
{
    if(!ui.sd.load_pending || PendingLoadBlockedByActiveRequest(worker))
        return;

    const uint16_t idx = ui.sd.load_pending_index;
    ui.sd.load_pending = false;
    CancelForPendingLoad(ui, worker);
    BeginUiRequest(worker, UiReqType::LoadWavIndex, idx);
    if(!StartLoadInternal(ui.sd, shared, idx))
        FailAndFinishUiRequest(worker, worker.project_restore);
}

static void StartQueuedUiRequest(AppUiState& ui,
                                 AppProjectState& project,
                                 AppEngineState& engine,
                                 AppSharedState& shared,
                                 AppWorkerState& worker,
                                 Params& params,
                                 const UiReq& req)
{
    BeginUiRequest(worker, req.type, req.a);

    switch(req.type)
    {
        case UiReqType::ScanSdWavs:
            if(!StartScan(ui.sd))
                FailAndFinishUiRequest(worker, worker.project_restore);
            break;
        case UiReqType::LoadWavIndex:
            if(!StartLoadInternal(ui.sd, shared, req.a))
            {
                ui.sd.load_in_progress = false;
                ui.sd.load_progress = 0;
                FailAndFinishUiRequest(worker, worker.project_restore);
            }
            break;
        case UiReqType::DeleteWavIndex:
            if(!DeleteWavAtIndex(ui.sd, req.a))
                worker.ui_req_result = -1;
            FinishRequest(worker, worker.project_restore);
            break;
        case UiReqType::NormalizeCurrent:
            if(!StartNormalize(ui, shared))
            {
                FailAndFinishUiRequest(worker, worker.project_restore);
            }
            else
            {
                ui.sd.load_in_progress = true;
                ui.sd.load_progress = 0;
            }
            break;
        case UiReqType::LoopFindCurrent:
            if(!LoopFindCurrent(ui, shared))
                worker.ui_req_result = -1;
            FinishRequest(worker, worker.project_restore);
            break;
        case UiReqType::SaveProject:
            if(!SaveProject(ui, project, engine, shared, worker, params))
                worker.ui_req_result = -1;
            FinishRequest(worker, worker.project_restore);
            break;
        case UiReqType::LoadProject:
            if(!LoadProject(ui, project, engine, shared, worker, params))
                FailAndFinishUiRequest(worker, worker.project_restore);
            break;
        case UiReqType::SaveRenderedWavCurrent:
            if(!StartSave(ui, shared))
                FailAndFinishUiRequest(worker, worker.project_restore);
            break;
        case UiReqType::RebuildCache:
        case UiReqType::LoadSample:
        case UiReqType::SavePreset:
            worker.ui_req_work_units_total = (req.type == UiReqType::RebuildCache) ? 2000u
                                           : (req.type == UiReqType::LoadSample)   ? 800u
                                                                                   : 200u;
            break;
        case UiReqType::None:
        default:
            FinishRequest(worker, worker.project_restore);
            break;
    }
}

static void MaybeStartNextUiRequest(AppUiState& ui,
                                    AppProjectState& project,
                                    AppEngineState& engine,
                                    AppSharedState& shared,
                                    AppWorkerState& worker,
                                    Params& params)
{
    if(worker.ui_req_busy)
        return;

    UiReq req{};
    if(!UiReq_Pop(worker, req))
        return;
    StartQueuedUiRequest(ui, project, engine, shared, worker, params, req);
}

static void FinalizeLoadProjectRequest(AppProjectState& project, AppWorkerState& worker)
{
    const uint8_t project_slot = RequestedProjectSlot(worker);
    if(worker.ui_req_result < 0)
        SetProjectSlotStatus(project, project_slot, "ERR");
    else
        SetProjectSlotStatus(project, project_slot, "LOADED");
    FinishRequest(worker, worker.project_restore);
}

static void StepActiveUiRequest(AppUiState& ui,
                                AppProjectState& project,
                                AppEngineState& engine,
                                AppSharedState& shared,
                                AppWorkerState& worker,
                                Params& params,
                                uint16_t budget_us)
{
    bool done = false;
    switch(worker.ui_req_active)
    {
        case UiReqType::ScanSdWavs:
            done = ScanStep(ui.sd);
            if(done)
            {
                worker.ui_req_progress = 100;
                FinishRequest(worker, worker.project_restore);
            }
            break;
        case UiReqType::LoadWavIndex:
            done = LoadStepInternal(ui.sd,
                                    worker,
                                    engine,
                                    shared,
                                    worker.project_restore,
                                    budget_us * 2u);
            worker.ui_req_progress = ui.sd.load_progress;
            if(done)
                FinishRequest(worker, worker.project_restore);
            break;
        case UiReqType::NormalizeCurrent:
            done = NormalizeStep(ui, shared, budget_us);
            worker.ui_req_progress = ui.sd.load_progress;
            if(done)
            {
                ui.sd.load_in_progress = false;
                FinishRequest(worker, worker.project_restore);
            }
            break;
        case UiReqType::LoopFindCurrent:
            FinishRequest(worker, worker.project_restore);
            break;
        case UiReqType::LoadProject:
            done = LoadStepInternal(ui.sd,
                                    worker,
                                    engine,
                                    shared,
                                    worker.project_restore,
                                    budget_us * 2u);
            worker.ui_req_progress = ui.sd.load_progress;
            if(done)
                FinalizeLoadProjectRequest(project, worker);
            break;
        case UiReqType::SaveRenderedWavCurrent:
            done = SaveStep(ui.sd, shared, worker, budget_us);
            worker.ui_req_progress = ui.sd.save_progress;
            if(done)
                FinishRequest(worker, worker.project_restore);
            break;
        case UiReqType::RebuildCache:
        case UiReqType::LoadSample:
        case UiReqType::SavePreset:
            StepFakeWork(worker, budget_us);
            if(worker.ui_req_work_units_done >= worker.ui_req_work_units_total)
                FinishRequest(worker, worker.project_restore);
            break;
        default:
            FinishRequest(worker, worker.project_restore);
            break;
    }

    (void)params;
}

static void MarkUiWorkerStateDirtyIfNeeded(AppUiState& ui,
                                           const AppWorkerState& worker,
                                           uint8_t prev_progress,
                                           bool prev_busy,
                                           UiReqType prev_active)
{
    if(worker.ui_req_progress != prev_progress || worker.ui_req_busy != prev_busy
       || worker.ui_req_active != prev_active)
        ui.ui_dirty = true;
}

// Top-level worker tick
void UiWorker_Tick(AppUiState& ui,
                   AppProjectState& project,
                   AppEngineState& engine,
                   AppSharedState& shared,
                   AppWorkerState& worker,
                   Params& params,
                   uint32_t now_ms,
                   uint16_t budget_us)
{
    (void)now_ms;
    const uint8_t prev_progress = worker.ui_req_progress;
    const bool prev_busy = worker.ui_req_busy;
    const UiReqType prev_active = worker.ui_req_active;

    MaybeHandlePendingLoad(ui, worker, shared);
    MaybeStartNextUiRequest(ui, project, engine, shared, worker, params);

    if(!worker.ui_req_busy)
        return;

    StepActiveUiRequest(ui, project, engine, shared, worker, params, budget_us);
    MarkUiWorkerStateDirtyIfNeeded(ui, worker, prev_progress, prev_busy, prev_active);
}
