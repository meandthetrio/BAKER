#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

static constexpr uint8_t kSteps = 16;

struct StepLock
{
    uint8_t enabled = 0;
    float   cutoff_norm = 0.0f;
};

struct Pattern
{
    StepLock steps[kSteps];
    uint8_t  step_index = 0;
};

struct PLocksState
{
    StepLock lock_a{};
    StepLock lock_b{};
    std::atomic<uint8_t>  lock_sel{0};
    std::atomic<uint32_t> lock_gen{0};
};

void PLocks_InitPattern(Pattern& pattern);
void PLocks_Publish(PLocksState& state, const StepLock& lock);
void PLocks_PublishCurrentStep(PLocksState& state, const Pattern& pattern);
