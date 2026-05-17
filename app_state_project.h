#pragma once

#include <cstdint>

enum class ProjectAction : uint8_t
{
    None = 0,
    Save,
    Load,
    Rename,
};

static constexpr uint8_t kProjectSlotCount = 8;
static constexpr uint8_t kProjectNameMax = 13;

// Main-thread project save/load slot selection, status, and project-owned restore coordination.
struct AppProjectState
{
    uint8_t current_project_slot = 0;
    ProjectAction project_action = ProjectAction::None;
    uint8_t project_action_slot = 0;
    char project_status[16] = {};
    char slot_names[kProjectSlotCount][kProjectNameMax] = {};
    bool slot_has_file[kProjectSlotCount] = {};
    bool metadata_scan_requested = false;
    bool metadata_scan_complete = false;
    uint8_t pending_rename_slot = 0;
    char pending_rename_name[kProjectNameMax] = {};
};
