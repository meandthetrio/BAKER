#include "sample_bake.h"

#include "sampler_sample.h"

#include <cstring>

// Pure seam-blend helpers defined in voice_engine_render_fetch.cpp. Reused here
// offline to bake the loop crossfade into PCM (no engine instance required).
uint32_t ComputeLoopSeamCrossfadeFrames(uint32_t start, uint32_t end, float amount);
float SampleAtLoopSeamCrossfade(const Sample* s,
                                uint32_t pos_frame,
                                float pos_frac,
                                uint32_t start,
                                uint32_t end,
                                uint32_t seam_frames,
                                float shape,
                                float sample_rate,
                                bool& used_xfade);

float DefaultSeamCrossfadeAmount(uint32_t start, uint32_t end)
{
    if(end <= start)
        return 0.0f;
    const float region = static_cast<float>(end - start);
    float amount = static_cast<float>(kDefaultSeamCrossfadeFrames) / region;
    if(amount > 0.5f)
        amount = 0.5f;
    if(amount < 0.0f)
        amount = 0.0f;
    return amount;
}

bool BakeLoopSeamToBuffer(const int16_t* raw,
                          uint32_t length,
                          uint32_t start,
                          uint32_t end,
                          float amount,
                          float shape,
                          float sample_rate,
                          int16_t* dst)
{
    if(raw == nullptr || dst == nullptr || length == 0u)
        return false;
    std::memcpy(dst, raw, static_cast<size_t>(length) * sizeof(int16_t));

    if(end > length)
        end = length;
    if(end <= start)
        return false;
    const uint32_t seam = ComputeLoopSeamCrossfadeFrames(start, end, amount);
    if(seam == 0u)
        return false;

    Sample src{};
    src.pcm = raw;
    src.length = length;
    src.sample_rate = (sample_rate > 0.0f) ? static_cast<uint32_t>(sample_rate) : 48000u;
    src.loop_start = start;
    src.loop_end = end;
    src.loop_enabled = true;

    const uint32_t seam_start = end - seam;
    for(uint32_t pos = seam_start; pos < end; ++pos)
    {
        bool used = false;
        float v = SampleAtLoopSeamCrossfade(&src, pos, 0.0f, start, end, seam, shape, sample_rate, used);
        float scaled = v * 32768.0f;
        if(scaled > 32767.0f)
            scaled = 32767.0f;
        else if(scaled < -32768.0f)
            scaled = -32768.0f;
        dst[pos] = static_cast<int16_t>(scaled);
    }
    return true;
}
