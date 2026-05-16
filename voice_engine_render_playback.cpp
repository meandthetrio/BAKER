#include "voice_engine_render_internal.h"

#include <cmath>

namespace
{
void WrapIntoLoopRegion(uint32_t end_frame,
                        uint32_t loop_start,
                        uint32_t& pos_frame,
                        float& pos_frac)
{
    if(end_frame <= loop_start)
        return;
    if(pos_frame < end_frame)
        return;

    const uint32_t loop_span = end_frame - loop_start;
    float overshoot = static_cast<float>(pos_frame - end_frame) + pos_frac;
    if(overshoot < 0.0f)
        overshoot = 0.0f;

    if(loop_span == 0u)
    {
        pos_frame = loop_start;
        pos_frac  = 0.0f;
        return;
    }

    const float span_f = static_cast<float>(loop_span);
    overshoot = std::fmod(overshoot, span_f);
    if(overshoot < 0.0f)
        overshoot += span_f;

    pos_frame = loop_start + static_cast<uint32_t>(overshoot);
    pos_frac  = overshoot - static_cast<float>(static_cast<uint32_t>(overshoot));
}

void ClampToStart(uint32_t start, uint32_t& pos_frame, float& pos_frac)
{
    if(pos_frame < start)
    {
        pos_frame = start;
        pos_frac  = 0.0f;
    }
}
} // namespace

void VoicePlayback_NormalizeStealFadeOutPositions(uint32_t end_frame,
                                                  uint32_t ls,
                                                  uint32_t start,
                                                  bool loop_enabled,
                                                  uint32_t& pos_frame,
                                                  float& pos_frac,
                                                  bool& gate)
{
    if(loop_enabled)
        WrapIntoLoopRegion(end_frame, ls, pos_frame, pos_frac);
    ClampToStart(start, pos_frame, pos_frac);
    if(!loop_enabled && end_frame > 0u && pos_frame >= end_frame)
    {
        gate = false;
        pos_frame = end_frame - 1u;
        pos_frac  = 0.0f;
    }
}

void VoicePlayback_NormalizeVoiceBlockStart(uint32_t end_frame,
                                            uint32_t ls,
                                            uint32_t start,
                                            bool loop_enabled,
                                            bool gate,
                                            uint32_t& pos_frame,
                                            float& pos_frac)
{
    if(loop_enabled && gate)
        WrapIntoLoopRegion(end_frame, ls, pos_frame, pos_frac);
    ClampToStart(start, pos_frame, pos_frac);
}

void VoicePlayback_ClampPosPastEndWhenGateOff(uint32_t end_frame,
                                              bool gate,
                                              uint32_t& pos_frame,
                                              float& pos_frac)
{
    if(!gate && end_frame > 0u && pos_frame >= end_frame)
    {
        pos_frame = end_frame - 1u;
        pos_frac  = 0.0f;
    }
}

void VoicePlayback_ClampPosToLastFrameIfValid(uint32_t end_frame,
                                              uint32_t& pos_frame,
                                              float& pos_frac)
{
    if(end_frame > 0u)
    {
        pos_frame = end_frame - 1u;
        pos_frac  = 0.0f;
    }
}

uint32_t VoicePlayback_ClampPlayheadFrame(uint32_t frame, uint32_t sample_length)
{
    if(sample_length == 0u)
        return 0u;
    const uint32_t pmax = sample_length - 1u;
    if(frame > pmax)
        frame = pmax;
    return frame;
}

float VoicePlayback_PosAsFloat(uint32_t frame, float frac)
{
    return static_cast<float>(frame) + frac;
}

void VoicePlayback_StepFadeIn(float& fade, float fade_step)
{
    fade += fade_step;
    if(fade > 1.0f)
        fade = 1.0f;
}

float VoicePlayback_ClampMixToOne(float x)
{
    float x_clamped = x;
    if(x_clamped > 1.0f)
        x_clamped = 1.0f;
    return x_clamped;
}

float VoicePlayback_FadeInMultiplier(float fade)
{
    return (fade < 1.0f) ? fade : 1.0f;
}
