#include "macros.h"
#include "app_state_shared.h"
#include "app_state.h"

static float Clamp01(float x)
{
    if(x < 0.0f) return 0.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

static float ClampBipolar(float x)
{
    if(x < -1.0f) return -1.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

static MacroDef MakeMacroA()
{
    MacroDef d{};
    d.count = 3;
    d.assigns[0] = {static_cast<uint8_t>(MacroTarget::FilterCutoff), 0.80f};
    d.assigns[1] = {static_cast<uint8_t>(MacroTarget::LfoDepth), 0.40f};
    d.assigns[2] = {static_cast<uint8_t>(MacroTarget::Drive), 0.30f};
    return d;
}

static MacroDef MakeMacroB()
{
    MacroDef d{};
    d.count = 3;
    d.assigns[0] = {static_cast<uint8_t>(MacroTarget::FilterCutoff), -0.50f};
    d.assigns[1] = {static_cast<uint8_t>(MacroTarget::Route0Amount), 0.50f};
    d.assigns[2] = {static_cast<uint8_t>(MacroTarget::EnvAmount), 0.30f};
    return d;
}

static const MacroDef kMacroDefs[kNumMacros] = {MakeMacroA(), MakeMacroB()};

void Macros_InitState(MacroState& state)
{
    for(uint8_t i = 0; i < kNumMacros; ++i)
        state.value[i] = 0.0f;
    state.selected = 0;
}

const MacroDef& Macros_GetDef(uint8_t idx)
{
    if(idx >= kNumMacros)
        idx = 0;
    return kMacroDefs[idx];
}

void Macros_Publish(AppSharedState& shared, const MacroState& state)
{
    const uint8_t sel = shared.performance.macro_sel.load(std::memory_order_relaxed) & 1u;
    MacroState* dst = (sel == 0) ? &shared.performance.macro_b : &shared.performance.macro_a;
    *dst = state;
    shared.performance.macro_sel.store(sel ^ 1u, std::memory_order_release);
    shared.performance.macro_gen.fetch_add(1u, std::memory_order_release);
}

void Macros_Publish(AppState& app, const MacroState& state)
{
    Macros_Publish(app.shared, state);
}

void Macros_Smooth(MacroState& current, const MacroState& target, float coeff)
{
    if(coeff < 0.0f)
        coeff = 0.0f;
    if(coeff > 1.0f)
        coeff = 1.0f;
    for(uint8_t i = 0; i < kNumMacros; ++i)
        current.value[i] += (target.value[i] - current.value[i]) * coeff;
    current.selected = target.selected;
}

void Macros_Apply(const MacroState& state,
                  float* cutoff_norm,
                  float* lfo_depth,
                  float* env_amount,
                  float* route0_amount,
                  float* drive)
{
    for(uint8_t mi = 0; mi < kNumMacros; ++mi)
    {
        float m = Clamp01(state.value[mi]);
        const MacroDef& def = kMacroDefs[mi];
        for(uint8_t ai = 0; ai < def.count; ++ai)
        {
            const MacroAssign& a = def.assigns[ai];
            const float delta = m * a.amount;
            switch(static_cast<MacroTarget>(a.target))
            {
                case MacroTarget::FilterCutoff:
                    if(cutoff_norm)
                        *cutoff_norm = Clamp01(*cutoff_norm + delta);
                    break;
                case MacroTarget::EnvAmount:
                    if(env_amount)
                        *env_amount = Clamp01(*env_amount + delta);
                    break;
                case MacroTarget::LfoDepth:
                    if(lfo_depth)
                        *lfo_depth = Clamp01(*lfo_depth + delta);
                    break;
                case MacroTarget::Route0Amount:
                    if(route0_amount)
                        *route0_amount = ClampBipolar(*route0_amount + delta);
                    break;
                case MacroTarget::Drive:
                    if(drive)
                        *drive = Clamp01(*drive + delta);
                    break;
                default:
                    break;
            }
        }
    }
}
