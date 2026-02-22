#include "plocks.h"

void PLocks_InitPattern(Pattern& pattern)
{
    pattern.step_index = 0;
    for(uint8_t i = 0; i < kSteps; ++i)
    {
        StepLock& s = pattern.steps[i];
        s.enabled = 1;
        s.cutoff_norm = (i % 2 == 0) ? 0.25f : 0.75f;
    }
}

void PLocks_Publish(PLocksState& state, const StepLock& lock)
{
    const uint8_t sel = state.lock_sel.load(std::memory_order_relaxed) & 1u;
    StepLock* dst = (sel == 0) ? &state.lock_b : &state.lock_a;
    *dst = lock;
    state.lock_sel.store(sel ^ 1u, std::memory_order_release);
    state.lock_gen.fetch_add(1u, std::memory_order_release);
}

void PLocks_PublishCurrentStep(PLocksState& state, const Pattern& pattern)
{
    const uint8_t idx = pattern.step_index % kSteps;
    PLocks_Publish(state, pattern.steps[idx]);
}
