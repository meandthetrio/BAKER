#pragma once

#include <cstdint>

static constexpr uint8_t kNumMacros = 2;
static constexpr uint8_t kMaxMacroTargets = 8;

enum class MacroTarget : uint8_t
{
    FilterCutoff = 0,
    EnvAmount = 1, // substitute for resonance
    LfoDepth = 2,
    Route0Amount = 3,
    Drive = 4,
    COUNT
};

struct MacroAssign
{
    uint8_t target = 0;
    float   amount = 0.0f;
};

struct MacroDef
{
    uint8_t    count = 0;
    MacroAssign assigns[kMaxMacroTargets];
};

struct MacroState
{
    float   value[kNumMacros]{0.0f, 0.0f};
    uint8_t selected = 0;
};

struct AppSharedState;
struct AppState;

void Macros_InitState(MacroState& state);
const MacroDef& Macros_GetDef(uint8_t idx);
void Macros_Publish(AppSharedState& shared, const MacroState& state);
void Macros_Publish(AppState& app, const MacroState& state);
void Macros_Smooth(MacroState& current, const MacroState& target, float coeff);
void Macros_Apply(const MacroState& state,
                  float* cutoff_norm,
                  float* lfo_depth,
                  float* env_amount,
                  float* route0_amount,
                  float* drive);
