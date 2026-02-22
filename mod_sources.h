#pragma once

#include <cstddef>
#include <cstdint>

struct GlobalLFO
{
    void Init(float sample_rate_hz);
    void SetRateHz(float rate_hz);
    void SetWave(uint8_t wave);
    void TickBlock(size_t n);
    float Value() const;

    float phase = 0.0f;
    float phase_inc = 0.0f;
    float sample_rate = 48000.0f;
    uint8_t wave = 0;
};

enum class ModEnvStage : uint8_t
{
    Idle = 0,
    Attack,
    Decay,
};

struct ModEnv
{
    void Init(float sample_rate_hz);
    void Reset();
    void Trigger(float attack_ms, float decay_ms);
    void TickBlock(size_t n);
    float Value() const;

    ModEnvStage stage = ModEnvStage::Idle;
    float       level = 0.0f;
    float       attack_inc = 0.0f;
    float       decay_inc = 0.0f;
    float       sample_rate = 48000.0f;
};
