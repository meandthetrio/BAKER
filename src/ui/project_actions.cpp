#include "project_actions.h"

#include "app_state.h"
#include "ui_requests.h"
#include "ui_screens.h"

#include <cstdio>

static void SetProjectStatusImmediate(AppState& app, uint8_t slot, const char* msg)
{
    std::snprintf(app.project_status,
                  sizeof(app.project_status),
                  "P%02u %s",
                  static_cast<unsigned>(slot + 1u),
                  msg ? msg : "");
}

static bool OpenProjectStatusScreen(AppState& app,
                                    ProjectAction action,
                                    uint8_t slot,
                                    const char* status)
{
    app.project_action = action;
    app.project_action_slot = slot;
    SetProjectStatusImmediate(app, slot, status);
    if(UiNav_Active(app.ui_nav) == UiScreenId::ProjectStatus)
        return true;
    return UiNav_Push(app.ui_nav, UiScreenId::ProjectStatus);
}

uint8_t ProjectActions_WrapSlot(int slot)
{
    while(slot < 0)
        slot += kProjectSlotCount;
    while(slot >= static_cast<int>(kProjectSlotCount))
        slot -= kProjectSlotCount;
    return static_cast<uint8_t>(slot);
}

bool ProjectActions_TriggerRequest(AppState& app, UiReqType req_type, uint8_t slot)
{
    const ProjectAction action = (req_type == UiReqType::SaveProject) ? ProjectAction::Save
                                                                      : ProjectAction::Load;
    OpenProjectStatusScreen(app, action, slot, (action == ProjectAction::Save) ? "SAVING"
                                                                                : "LOADING");
    const UiReq req{req_type, slot, 0};
    if(!UiReq_Push(app, req))
        SetProjectStatusImmediate(app, slot, "ERR");
    app.ui_dirty = true;
    return true;
}
