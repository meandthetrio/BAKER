#include "voice_engine_render_internal.h"

#include <cmath>

static float s_sqrt_lut[256] = {};

void VoiceRenderFetch_InitSqrtLut()
{
    for(int i = 0; i < 256; ++i)
        s_sqrt_lut[i] = std::sqrt(static_cast<float>(i) / 255.0f);
}

float SampleAtLinear(const Sample* s, float pos, bool wrap_end)
{
    if(s == nullptr || s->pcm == nullptr || s->length == 0)
        return 0.0f;
    const uint32_t i = static_cast<uint32_t>(pos);
    if(i >= s->length)
        return 0.0f;
    const float frac = pos - static_cast<float>(i);
    const int16_t a = s->pcm[i];
    const int16_t b = (i + 1 < s->length) ? s->pcm[i + 1] : (wrap_end ? s->pcm[0] : a);
    const float fa = static_cast<float>(a) * (1.0f / 32768.0f);
    const float fb = static_cast<float>(b) * (1.0f / 32768.0f);
    return fa + frac * (fb - fa);
}

float SampleAtLinearRegion(const Sample* s,
                           float pos,
                           uint32_t start,
                           uint32_t end,
                           bool loop_enabled,
                           uint32_t loop_start,
                           uint32_t loop_end)
{
    if(s == nullptr || s->pcm == nullptr || s->length == 0)
        return 0.0f;
    if(end <= start || end > s->length)
        end = s->length;
    if(pos < (float)start || pos >= (float)end)
        return 0.0f;

    const uint32_t i = static_cast<uint32_t>(pos);
    if(i < start || i >= end)
        return 0.0f;
    const float frac = pos - static_cast<float>(i);
    const int16_t a = s->pcm[i];
    uint32_t next = i + 1;
    if(next >= end)
    {
        if(loop_enabled && loop_start < loop_end)
            next = loop_start;
        else
            next = i;
    }
    const int16_t b = s->pcm[next];
    const float fa = static_cast<float>(a) * (1.0f / 32768.0f);
    const float fb = static_cast<float>(b) * (1.0f / 32768.0f);
    return fa + frac * (fb - fa);
}

uint32_t ComputeLoopSeamCrossfadeFrames(uint32_t start, uint32_t end, float amount)
{
    if(end <= start + 1)
        return 0;
    if(amount <= 0.0f)
        return 0;
    if(amount > 0.5f)
        amount = 0.5f;

    const uint32_t region_frames = end - start;
    const uint32_t max_frames = region_frames / 2;
    if(max_frames == 0)
        return 0;

    uint32_t frames = static_cast<uint32_t>((static_cast<float>(region_frames) * amount) + 0.5f);
    if(frames == 0)
        frames = 1;
    if(frames > max_frames)
        frames = max_frames;
    return frames;
}

float ComputeLoopSeamCrossfadeWeight(float mix, float shape, bool fade_in)
{
    if(mix < 0.0f)
        mix = 0.0f;
    if(mix > 1.0f)
        mix = 1.0f;
    if(shape < 0.0f)
        shape = 0.0f;
    if(shape > 1.0f)
        shape = 1.0f;

    const float linear = fade_in ? mix : (1.0f - mix);
    const float lut_in = fade_in ? mix : (1.0f - mix);
    const int lut_idx  = static_cast<int>(lut_in * 255.0f + 0.5f);
    const float equal_power = s_sqrt_lut[lut_idx < 255 ? lut_idx : 255];
    return linear + (equal_power - linear) * shape;
}

float SampleAtLoopSeamCrossfade(const Sample* s,
                                float pos,
                                uint32_t start,
                                uint32_t end,
                                uint32_t seam_frames,
                                float shape,
                                float sample_rate,
                                bool& used_xfade)
{
    (void)sample_rate;
    used_xfade = false;
    if(seam_frames == 0 || end <= start + seam_frames)
        return SampleAtLinearRegion(s, pos, start, end, true, start, end);

    const float seam_start = static_cast<float>(end - seam_frames);
    if(pos < seam_start)
        return SampleAtLinearRegion(s, pos, start, end, true, start, end);

    float mix = (pos - seam_start) / static_cast<float>(seam_frames);
    if(mix < 0.0f)
        mix = 0.0f;
    if(mix > 1.0f)
        mix = 1.0f;

    const float seam_pos = static_cast<float>(start) + (pos - seam_start);
    const float tail = SampleAtLinearRegion(s, pos, start, end, true, start, end);
    const float head = SampleAtLinearRegion(s, seam_pos, start, end, true, start, end);
    const float tail_weight = ComputeLoopSeamCrossfadeWeight(mix, shape, false);
    const float head_weight = ComputeLoopSeamCrossfadeWeight(mix, shape, true);
    used_xfade = true;
    return tail * tail_weight + head * head_weight;
}

float VoiceRenderFetch_VoiceStream(const Sample* sample,
                                   float pos,
                                   float gain,
                                   bool layer_loop_voice,
                                   uint32_t start,
                                   uint32_t end,
                                   uint32_t seam_frames,
                                   float loop_shape,
                                   float sample_rate,
                                   bool& used_seam_xfade,
                                   bool use_edit,
                                   bool region_loop_enabled,
                                   uint32_t ls_i,
                                   uint32_t le_i,
                                   bool gate_for_wrap)
{
    if(layer_loop_voice)
        return SampleAtLoopSeamCrossfade(sample,
                                         pos,
                                         start,
                                         end,
                                         seam_frames,
                                         loop_shape,
                                         sample_rate,
                                         used_seam_xfade)
            * gain;
    if(use_edit)
        return SampleAtLinearRegion(sample,
                                    pos,
                                    start,
                                    end,
                                    region_loop_enabled,
                                    ls_i,
                                    le_i) * gain;

    const bool wrap_end
        = VoiceRenderLoop_FullSampleWrapGate(sample, region_loop_enabled, gate_for_wrap);
    return SampleAtLinear(sample, pos, wrap_end) * gain;
}
