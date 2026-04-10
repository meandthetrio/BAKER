#include "project_actions.h"

#include "app_state_project.h"
#include "app_state_ui.h"
#include "app_state_worker.h"
#include "app_state.h"
#include "ui_requests.h"
#include "ui_screens.h"

#include <cstdio>

static void SetProjectStatusImmediate(AppProjectState& project, uint8_t slot, const char* msg)
{
    std::snprintf(project.project_status,
                  sizeof(project.project_status),
                  "P%02u %s",
                  static_cast<unsigned>(slot + 1u),
                  msg ? msg : "");
}

static bool OpenProjectStatusScreen(AppUiState& ui,
                                    AppProjectState& project,
                                    ProjectAction action,
                                    uint8_t slot,
                                    const char* status)
{
    project.project_action = action;
    project.project_action_slot = slot;
    SetProjectStatusImmediate(project, slot, status);
    if(UiNav_Active(ui.ui_nav) == UiScreenId::ProjectStatus)
        return true;
    return UiNav_Push(ui.ui_nav, UiScreenId::ProjectStatus);
}

uint8_t ProjectActions_WrapSlot(int slot)
{
    while(slot < 0)
        slot += kProjectSlotCount;
    while(slot >= static_cast<int>(kProjectSlotCount))
        slot -= kProjectSlotCount;
    return static_cast<uint8_t>(slot);
}

bool ProjectActions_TriggerRequest(AppUiState& ui,
                                   AppProjectState& project,
                                   AppWorkerState& worker,
                                   UiReqType req_type,
                                   uint8_t slot)
{
    const ProjectAction action = (req_type == UiReqType::SaveProject) ? ProjectAction::Save
                                                                      : ProjectAction::Load;
    OpenProjectStatusScreen(ui,
                            project,
                            action,
                            slot,
                            (action == ProjectAction::Save) ? "SAVING" : "LOADING");
    const UiReq req{req_type, slot, 0};
    if(!UiReq_Push(ui, worker, req))
        SetProjectStatusImmediate(project, slot, "ERR");
    ui.ui_dirty = true;
    return true;
}

bool ProjectActions_TriggerRequest(AppState& app, UiReqType req_type, uint8_t slot)
{
    return ProjectActions_TriggerRequest(app.ui, app.project, app.worker, req_type, slot);
}
