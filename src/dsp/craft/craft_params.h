#pragma once

#include <cstdint>

// CRAFT param descriptor table — the single source of truth for the CRAFT
// (Samples) screen. It drives the UI (focus count, value editing/clamping,
// label + value display) AND tells the DSP how many params each plugin owns.
//
// Each plugin stores 6 raw uint8 param values per slot (app_state_ui.h:
// craft_param[slot][plugin][6]). How a raw value maps to DSP units is decided
// inside each effect's processor (SetParams); this table only describes the
// param for the UI and the shared param_count.
//
// To bring a not-yet-implemented plugin online: fill in its descriptor here
// (param_count + the 6 CraftParamDesc entries) and the UI picks it up with no
// further screen changes. See memory: craft-plugins-spec.md (V2).

namespace craft {

static constexpr uint8_t kCraftSlotCount   = 3u;
static constexpr uint8_t kCraftPluginCount = 7u; // ----, copy, dial, snap, warm, howl, warp
static constexpr uint8_t kCraftMaxParams   = 6u;

// Plugin ids — index into kCraftPluginLabels (UI) and the CraftChain dispatch.
enum CraftPlugin : uint8_t
{
    kCraftPluginNone = 0u,
    kCraftPluginCopy = 1u, // generation loss / resample
    kCraftPluginDial = 2u, // ring-mod / between stations
    kCraftPluginSnap = 3u, // transient burn
    kCraftPluginWarm = 4u, // body saturation
    kCraftPluginHowl = 5u, // feedback fold
    kCraftPluginWarp = 6u, // tape wow & flutter
};

enum class CraftParamKind : uint8_t
{
    Enum,   // discrete: value in [0, count); displayed via enum_labels
    Scalar, // continuous: value in [0, count] (count is the max, e.g. 100)
};

struct CraftParamDesc
{
    const char*        label;       // OLED label, <= 6 chars
    CraftParamKind     kind;
    uint8_t            count;       // Enum: #options; Scalar: max value (e.g. 100)
    const char* const* enum_labels; // Enum: count entries; nullptr for Scalar
};

struct CraftPluginDesc
{
    uint8_t        param_count; // 0 = not yet implemented / no params
    CraftParamDesc params[kCraftMaxParams];
};

// One slot's configuration: which plugin + its 6 raw params. Snapshotted by the
// UI and handed to a CraftChain (audio thread audition / worker render). POD so
// it can live in the cross-thread shared state without pulling in DSP headers.
struct CraftSlotConfig
{
    uint8_t plugin = kCraftPluginNone;
    uint8_t param[kCraftMaxParams] = {0u, 0u, 0u, 0u, 0u, 0u};
};

struct CraftChainConfig
{
    CraftSlotConfig slots[kCraftSlotCount];
};

// Accessors.
const CraftPluginDesc& CraftGetPluginDesc(uint8_t plugin);
uint8_t                CraftPluginParamCount(uint8_t plugin);
// Returns nullptr if (plugin, param) is out of range / undefined.
const CraftParamDesc* CraftGetParamDesc(uint8_t plugin, uint8_t param);

} // namespace craft
