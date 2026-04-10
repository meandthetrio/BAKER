#include "mod_sources.h"

#include <cmath>

static constexpr float kTwoPi = 6.2831853071795864769f;

void GlobalLFO::Init(float sample_rate_hz)
{
    sample_rate = (sample_rate_hz > 0.0f) ? sample_rate_hz : 48000.0f;
    phase = 0.0f;
    phase_inc = 0.0f;
}

void GlobalLFO::SetRateHz(float rate_hz)
{
    if(rate_hz < 0.0f)
        rate_hz = 0.0f;
    phase_inc = rate_hz / sample_rate;
}

void GlobalLFO::SetWave(uint8_t w)
{
    wave = (w == 0) ? 0 : 1;
}

void GlobalLFO::TickBlock(size_t n)
{
    phase += phase_inc * static_cast<float>(n);
    if(phase >= 1.0f || phase < 0.0f)
        phase -= std::floor(phase);
}

float GlobalLFO::Value() const
{
    if(wave == 0)
        return static_cast<float>(std::sin(phase * kTwoPi));
    return (phase < 0.5f) ? 1.0f : -1.0f;
}

void ModEnv::Init(float sample_rate_hz)
{
    sample_rate = (sample_rate_hz > 0.0f) ? sample_rate_hz : 48000.0f;
    Reset();
}

void ModEnv::Reset()
{
    stage = ModEnvStage::Idle;
    level = 0.0f;
    attack_inc = 0.0f;
    decay_inc = 0.0f;
}

void ModEnv::Trigger(float attack_ms, float decay_ms)
{
    if(attack_ms < 0.0f)
        attack_ms = 0.0f;
    if(decay_ms < 0.0f)
        decay_ms = 0.0f;

    int attack_samples = static_cast<int>(sample_rate * 0.001f * attack_ms);
    int decay_samples  = static_cast<int>(sample_rate * 0.001f * decay_ms);
    if(attack_samples < 1)
        attack_samples = 1;
    if(decay_samples < 1)
        decay_samples = 1;

    attack_inc = 1.0f / static_cast<float>(attack_samples);
    decay_inc  = 1.0f / static_cast<float>(decay_samples);
    level = 0.0f;
    stage = ModEnvStage::Attack;
}

void ModEnv::TickBlock(size_t n)
{
    if(stage == ModEnvStage::Idle || n == 0)
        return;

    const float step = static_cast<float>(n);
    if(stage == ModEnvStage::Attack)
    {
        level += attack_inc * step;
        if(level >= 1.0f)
        {
            level = 1.0f;
            stage = ModEnvStage::Decay;
        }
    }

    if(stage == ModEnvStage::Decay)
    {
        level -= decay_inc * step;
        if(level <= 0.0f)
        {
            level = 0.0f;
            stage = ModEnvStage::Idle;
        }
    }
}

float ModEnv::Value() const
{
    return level;
}
