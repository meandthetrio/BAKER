#pragma once

#include <cstdint>

struct SampleEdit
{
    uint32_t start_frame = 0;
    uint32_t end_frame = 0;
    uint32_t loop_start = 0;
    uint32_t loop_end = 0;
    uint8_t  loop_enable = 0;
    float    gain = 1.0f;
    // Peak-normalization gain for the current window, computed at load / on trim
    // edit. Ungated: the voice applies it only when normalization is enabled, so
    // the toggle is instant with no rescan. 1.0 = no normalization.
    float    norm_gain = 1.0f;
};

static inline SampleEdit SampleEdit_Default(uint32_t frames)
{
    SampleEdit e{};
    e.start_frame = 0;
    e.end_frame = frames;
    e.loop_start = 0;
    e.loop_end = frames;
    e.loop_enable = 0;
    e.gain = 1.0f;
    e.norm_gain = 1.0f;
    return e;
}

// Peak-normalization gain so the loudest sample in [start,end) lands at -12 dBFS.
// Bidirectional (boosts quiet windows, cuts hot ones); the boost is clamped so a
// near-silent window can't explode. Ungated — caller/voice decides whether to use it.
static inline float SampleEdit_ComputeNormGain(const int16_t* pcm,
                                               uint32_t start,
                                               uint32_t end)
{
    if(pcm == nullptr || end <= start)
        return 1.0f;
    int peak = 1;
    for(uint32_t i = start; i < end; ++i)
    {
        int a = pcm[i];
        if(a < 0)
            a = -a;
        if(a > peak)
            peak = a;
    }
    // -12 dBFS target in int16 units: 10^(-12/20) * 32768 ≈ 8231.
    constexpr float kTargetInt = 8231.0f;
    float g = kTargetInt / static_cast<float>(peak);
    if(g > 8.0f) // clamp boost to +18 dB for near-silent windows
        g = 8.0f;
    return g;
}

static inline void SampleEdit_Clamp(SampleEdit& e, uint32_t frames)
{
    if(frames == 0)
    {
        e.start_frame = 0;
        e.end_frame = 0;
        e.loop_start = 0;
        e.loop_end = 0;
        e.loop_enable = 0;
        e.gain = 1.0f;
        return;
    }

    if(e.end_frame > frames)
        e.end_frame = frames;
    if(e.start_frame >= e.end_frame)
        e.start_frame = (e.end_frame > 0) ? (e.end_frame - 1) : 0;

    if(e.loop_start < e.start_frame)
        e.loop_start = e.start_frame;
    if(e.loop_start >= e.end_frame)
        e.loop_start = (e.end_frame > 0) ? (e.end_frame - 1) : 0;

    if(e.loop_end <= e.loop_start)
        e.loop_end = e.loop_start + 1;
    if(e.loop_end > e.end_frame)
        e.loop_end = e.end_frame;

    if(e.gain < 0.0f)
        e.gain = 0.0f;
}
