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

#include <cstdio>

using namespace daisy;

static volatile uint32_t s_work_sink = 0;

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

static void BeginUiRequest(AppWorkerState& worker, UiReqType type, uint16_t arg0, uint16_t arg1)
{
    worker.ui_req_busy = true;
    worker.ui_req_active = type;
    worker.ui_req_progress = 0;
    worker.ui_req_result = 0;
    worker.ui_req_arg0 = arg0;
    worker.ui_req_arg1 = arg1;
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

static void CancelForPendingLoad(AppUiState& ui, AppWorkerState& worker, AppSharedState& shared)
{
    if(!worker.ui_req_busy)
        return;

    if(worker.ui_req_active == UiReqType::ScanSdWavs)
        CancelScan(ui.sd);
    if(worker.ui_req_active == UiReqType::LoadWavIndex)
        CancelLoad(ui, worker, shared);
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
    CancelForPendingLoad(ui, worker, shared);
    BeginUiRequest(worker, UiReqType::LoadWavIndex, idx, 0);
    if(!StartLoadInternal(ui.sd, shared, idx))
    {
        SdWavLoad_SetBusy(shared, ui.sd, false);
        FailAndFinishUiRequest(worker, worker.project_restore);
    }
}

static void StartQueuedUiRequest(AppUiState& ui,
                                 AppProjectState& project,
                                 AppEngineState& engine,
                                 AppSharedState& shared,
                                 AppWorkerState& worker,
                                 Params& params,
                                 const UiReq& req)
{
    BeginUiRequest(worker, req.type, req.a, req.b);

    switch(req.type)
    {
        case UiReqType::ScanSdWavs:
            if(!StartScan(ui.sd, shared))
                FailAndFinishUiRequest(worker, worker.project_restore);
            break;
        case UiReqType::LoadWavIndex:
            if(!StartLoadInternal(ui.sd, shared, req.a))
            {
                ui.sd.load_in_progress = false;
                ui.sd.load_progress = 0;
                SdWavLoad_SetBusy(shared, ui.sd, false);
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
        case UiReqType::ScanProjectSlots:
            if(!ScanProjectSlots(ui, project))
                worker.ui_req_result = -1;
            FinishRequest(worker, worker.project_restore);
            break;
        case UiReqType::RenameProject:
            if(!RenameProject(ui, project, worker))
                worker.ui_req_result = -1;
            FinishRequest(worker, worker.project_restore);
            break;
        case UiReqType::RenameWavIndex:
            if(!RenameWavAtIndex(ui.sd, req.a, ui.project_rename_draft))
                worker.ui_req_result = -1;
            else
            {
                SdBrowser_ClearList(ui.sd);
                ui.sd.scan_done = false;
                ui.sd.scan_in_progress = false;
            }
            FinishRequest(worker, worker.project_restore);
            break;
        case UiReqType::UpdateProjectStyle:
            if(!UpdateProjectStyle(ui, project, worker))
                worker.ui_req_result = -1;
            FinishRequest(worker, worker.project_restore);
            break;
        case UiReqType::SaveRenderedWavCurrent:
            if(!StartSave(ui, shared, req.a != 0u))
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
    {
        SetProjectSlotStatus(project, project_slot, "LOADED");
        project.has_active_project_slot = true;
        project.active_project_slot = project_slot;
    }
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
