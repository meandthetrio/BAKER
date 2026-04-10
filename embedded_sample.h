#pragma once

#include <cmath>
#include <cstdint>

#include "sampler_sample.h"

static constexpr uint32_t kEmbeddedSampleRate   = 48000;
static constexpr uint32_t kEmbeddedSampleLength = 256; // single-cycle
static constexpr uint8_t  kEmbeddedSampleRootKey = 60;

alignas(4) static int16_t kEmbeddedSamplePcm[kEmbeddedSampleLength];

static inline void InitEmbeddedSample()
{
    static bool initialized = false;
    if(initialized)
        return;
    initialized = true;

    const float two_pi = 6.2831853071795864769f;
    float max_abs = 0.0f;

    for(uint32_t i = 0; i < kEmbeddedSampleLength; ++i)
    {
        const float phase = static_cast<float>(i) / static_cast<float>(kEmbeddedSampleLength);
        const float saw = 2.0f * phase - 1.0f;
        const float shaped = 0.85f * saw
                             + 0.10f * std::sin(two_pi * 2.0f * phase)
                             + 0.05f * std::sin(two_pi * 3.0f * phase);

        if(std::fabs(shaped) > max_abs)
            max_abs = std::fabs(shaped);

        kEmbeddedSamplePcm[i] = static_cast<int16_t>(shaped * 32767.0f);
    }

    if(max_abs > 0.0f)
    {
        const float inv = 1.0f / max_abs;
        for(uint32_t i = 0; i < kEmbeddedSampleLength; ++i)
        {
            float v = static_cast<float>(kEmbeddedSamplePcm[i]) / 32767.0f;
            v *= inv;
            if(v > 1.0f) v = 1.0f;
            if(v < -1.0f) v = -1.0f;
            kEmbeddedSamplePcm[i] = static_cast<int16_t>(v * 32767.0f);
        }
    }
}

static const Sample kEmbeddedSample = {kEmbeddedSamplePcm,
                                       kEmbeddedSampleLength,
                                       kEmbeddedSampleRate,
                                       kEmbeddedSampleRootKey,
                                       0,
                                       kEmbeddedSampleLength,
                                       true};

static inline const Sample* GetEmbeddedSample()
{
    InitEmbeddedSample();
    return &kEmbeddedSample;
}
