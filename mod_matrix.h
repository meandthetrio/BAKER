#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

enum class ModSource : uint8_t
{
    LFO = 0,
    ModEnv = 1,
    COUNT
};

enum class ModDest : uint8_t
{
    FilterCutoff = 0,
    Pitch = 1,
    COUNT
};

struct ModRoute
{
    uint8_t src = 0;
    uint8_t dst = 0;
    float   amount = 0.0f;
    uint8_t enabled = 0;
};

static constexpr size_t kMaxModRoutes = 4;

struct ModMatrixState
{
    ModRoute routes_a[kMaxModRoutes];
    ModRoute routes_b[kMaxModRoutes];
    std::atomic<uint32_t> routes_gen{0};
    std::atomic<uint8_t>  routes_sel{0};
};

void ModMatrix_InitDefaults(ModMatrixState& state, ModRoute* ui_routes);
void ModMatrix_Publish(ModMatrixState& state, const ModRoute* ui_routes);
