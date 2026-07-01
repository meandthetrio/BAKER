#pragma once

#include <cstdint>

enum class ProjectAction : uint8_t
{
    None = 0,
    Save,
    Load,
    Rename,
    Style,
    Delete,
};

static constexpr uint8_t kProjectSlotCount = 64;
static constexpr uint8_t kProjectNameMax = 13;

enum class ProjectStyleId : uint8_t
{
    None = 0,
    Pad,
    Pluck,
    Bass,
    Key,
    COUNT,
};

static constexpr uint8_t kProjectStyleCount = static_cast<uint8_t>(ProjectStyleId::COUNT);

inline ProjectStyleId ProjectStyleFromStored(uint8_t raw)
{
    return (raw < kProjectStyleCount) ? static_cast<ProjectStyleId>(raw) : ProjectStyleId::None;
}

inline uint8_t ProjectStyleToStored(ProjectStyleId style)
{
    const uint8_t raw = static_cast<uint8_t>(style);
    return (raw < kProjectStyleCount) ? raw : 0u;
}

// Main-thread project save/load slot selection, status, and project-owned restore coordination.
struct AppProjectState
{
    uint8_t current_project_slot = 0;
    bool has_active_project_slot = false;
    uint8_t active_project_slot = 0;
    ProjectAction project_action = ProjectAction::None;
    uint8_t project_action_slot = 0;
    char project_status[16] = {};
    char slot_names[kProjectSlotCount][kProjectNameMax] = {};
    ProjectStyleId slot_styles[kProjectSlotCount] = {};
    bool slot_has_file[kProjectSlotCount] = {};
    uint8_t visible_slot_order[kProjectSlotCount] = {};
    uint8_t visible_slot_count = kProjectSlotCount;
    bool metadata_scan_requested = false;
    bool metadata_scan_complete = false;
    uint8_t pending_rename_slot = 0;
    char pending_rename_name[kProjectNameMax] = {};
};
