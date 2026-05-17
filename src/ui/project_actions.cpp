#include "project_actions.h"

#include "app_state_project.h"
#include "app_state_ui.h"
#include "app_state_worker.h"
#include "ui_requests.h"
#include "ui_screens.h"

#include <cstdio>
#include <cstring>

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

const char* ProjectActions_DisplayName(const AppProjectState& project, uint8_t slot)
{
    if(slot < kProjectSlotCount)
    {
        if(!project.slot_has_file[slot])
            return "----";
        if(project.slot_names[slot][0] != '\0')
            return project.slot_names[slot];
    }

    static char fallback[16];
    std::snprintf(fallback, sizeof(fallback), "PROJECT %02u", static_cast<unsigned>(slot + 1u));
    return fallback;
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
    ProjectAction action = ProjectAction::Load;
    const char* status = "LOADING";
    if(req_type == UiReqType::SaveProject)
    {
        action = ProjectAction::Save;
        status = "SAVING";
    }
    else if(req_type == UiReqType::RenameProject)
    {
        action = ProjectAction::Rename;
        status = "RENAMING";
    }

    OpenProjectStatusScreen(ui,
                            project,
                            action,
                            slot,
                            status);
    const UiReq req{req_type, slot, 0};
    if(!UiReq_Push(ui, worker, req))
        SetProjectStatusImmediate(project, slot, "ERR");
    ui.ui_dirty = true;
    return true;
}
