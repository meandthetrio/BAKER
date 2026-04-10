#pragma once

#include <cstdint>

#include "sample_edit.h"
#include "storage_limits.h"

enum class ProjectAction : uint8_t
{
    None = 0,
    Save,
    Load,
};

static constexpr uint8_t kProjectSlotCount = 8;

// Main-thread project save/load slot selection, status, and project-owned restore coordination.
struct AppProjectState
{
    uint8_t current_project_slot = 0;
    ProjectAction project_action = ProjectAction::None;
    uint8_t project_action_slot = 0;
    char project_status[16] = {};
    uint8_t project_edit_pending_mask = 0;
    SampleEdit project_pending_edit[kSdSampleSlots]{};
};
