#pragma once

#include <cmath>
#include <cstdint>

#include "sampler_sample.h"

static constexpr uint32_t kLongSampleRate   = 48000;
static constexpr uint32_t kLongSampleLength = 48000; // ~1.0s
static constexpr uint8_t  kLongSampleRootKey = 60;
static constexpr uint32_t kLongLoopStart = 8000;
static constexpr uint32_t kLongLoopEnd   = 40000; // exclusive

alignas(4) static int16_t kLongSamplePcm[kLongSampleLength];

static inline void InitEmbeddedLongSample()
{
    static bool initialized = false;
    if(initialized)
        return;
    initialized = true;

    // Build a 256-sample irregular saw wavetable.
    static constexpr uint32_t kWaveLen = 256;
    float table[kWaveLen];
    const float two_pi = 6.2831853071795864769f;
    float max_abs = 0.0f;
    for(uint32_t i = 0; i < kWaveLen; ++i)
    {
        const float phase = static_cast<float>(i) / static_cast<float>(kWaveLen);
        const float saw = 2.0f * phase - 1.0f;
        const float shaped = 0.85f * saw
                             + 0.10f * sinf(two_pi * 2.0f * phase)
                             + 0.05f * sinf(two_pi * 3.0f * phase);
        table[i] = shaped;
        if(fabsf(shaped) > max_abs)
            max_abs = fabsf(shaped);
    }
    if(max_abs < 1e-6f)
        max_abs = 1.0f;
    const float norm = 1.0f / max_abs;
    for(uint32_t i = 0; i < kWaveLen; ++i)
        table[i] *= norm;

    const uint32_t attack_samples = 480;   // ~10ms
    const uint32_t tail_samples   = 9600;  // ~200ms
    const uint32_t tail_start     = (kLongSampleLength > tail_samples)
                                        ? (kLongSampleLength - tail_samples)
                                        : 0;

    uint32_t phase_idx = 0;
    uint32_t rng = 0x12345678u;

    for(uint32_t i = 0; i < kLongSampleLength; ++i)
    {
        // Base wavetable tone
        float v = table[phase_idx];
        phase_idx++;
        if(phase_idx >= kWaveLen)
            phase_idx = 0;

        // Short noise transient at start
        if(i < attack_samples)
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            const float noise = (static_cast<int32_t>(rng & 0xFFFFu) - 32768) / 32768.0f;
            const float blend = 1.0f - (static_cast<float>(i) / attack_samples);
            v = 0.85f * v + 0.15f * noise * blend;
        }

        // Sample-shape envelope: fast attack, sustain, then tail fade.
        float env = 1.0f;
        if(i < attack_samples)
            env = static_cast<float>(i) / attack_samples;
        else if(i >= tail_start)
            env = 1.0f - (static_cast<float>(i - tail_start) / tail_samples);

        v *= env;
        if(v > 1.0f) v = 1.0f;
        if(v < -1.0f) v = -1.0f;
        kLongSamplePcm[i] = static_cast<int16_t>(v * 32767.0f);
    }
}

static const Sample kEmbeddedLongSample = {kLongSamplePcm,
                                           kLongSampleLength,
                                           kLongSampleRate,
                                           kLongSampleRootKey,
                                           kLongLoopStart,
                                           kLongLoopEnd,
                                           true};

static inline const Sample* GetEmbeddedLongSample()
{
    InitEmbeddedLongSample();
    return &kEmbeddedLongSample;
}
