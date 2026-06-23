#include "craft/craft_params.h"

namespace craft {

// ---- copy (generation loss / resample) value-label arrays ----
static const char* const kCopyRateLabels[6]  = {"48k", "32k", "27k", "22k", "16k", "12k"};
static const char* const kCopyBitsLabels[5]  = {"16", "12", "10", "8", "6"};
static const char* const kCopyToneLabels[5]  = {"clean", "soft", "ring", "leak", "bad"};
static const char* const kCopyCurveLabels[4] = {"lin", "comp", "warp", "noisy"};

// Plugin descriptor table, indexed by CraftPlugin id. Plugins with param_count
// == 0 are name-only (not yet implemented) and the UI shows no param grid.
static const CraftPluginDesc kPluginDescs[kCraftPluginCount] = {
    // 0: ---- (none)
    {0u, {}},
    // 1: copy — rate, bits, drive, tone, curve, wear
    {6u,
     {{"rate", CraftParamKind::Enum, 6u, kCopyRateLabels},
      {"bits", CraftParamKind::Enum, 5u, kCopyBitsLabels},
      {"drive", CraftParamKind::Scalar, 100u, nullptr},
      {"tone", CraftParamKind::Enum, 5u, kCopyToneLabels},
      {"curve", CraftParamKind::Enum, 4u, kCopyCurveLabels},
      {"wear", CraftParamKind::Scalar, 100u, nullptr}}},
    // 2: dial   — not yet implemented
    {0u, {}},
    // 3: snap   — not yet implemented
    {0u, {}},
    // 4: warm   — not yet implemented
    {0u, {}},
    // 5: howl   — not yet implemented
    {0u, {}},
    // 6: warp   — not yet implemented
    {0u, {}},
};

const CraftPluginDesc& CraftGetPluginDesc(uint8_t plugin)
{
    if(plugin >= kCraftPluginCount)
        return kPluginDescs[0];
    return kPluginDescs[plugin];
}

uint8_t CraftPluginParamCount(uint8_t plugin)
{
    return CraftGetPluginDesc(plugin).param_count;
}

const CraftParamDesc* CraftGetParamDesc(uint8_t plugin, uint8_t param)
{
    const CraftPluginDesc& d = CraftGetPluginDesc(plugin);
    if(param >= d.param_count)
        return nullptr;
    return &d.params[param];
}

} // namespace craft
