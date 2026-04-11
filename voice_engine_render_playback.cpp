#include "voice_engine_render_internal.h"

void VoicePlayback_NormalizeStealXFadePositions(float length_f,
                                                float ls,
                                                uint32_t start,
                                                bool old_loop_enabled,
                                                bool new_loop_enabled,
                                                float& old_pos,
                                                float& new_pos,
                                                bool& old_gate,
                                                bool& new_gate)
{
    if(old_loop_enabled && old_pos >= length_f)
        old_pos = ls + (old_pos - length_f);
    if(new_loop_enabled && new_pos >= length_f)
        new_pos = ls + (new_pos - length_f);
    if(old_pos < static_cast<float>(start))
        old_pos = static_cast<float>(start);
    if(new_pos < static_cast<float>(start))
        new_pos = static_cast<float>(start);
    if(!old_loop_enabled && old_pos >= length_f && length_f > 0.0f)
    {
        old_gate = false;
        old_pos = length_f - 1.0f;
    }
    if(!new_loop_enabled && new_pos >= length_f && length_f > 0.0f)
    {
        new_gate = false;
        new_pos = length_f - 1.0f;
    }
}

void VoicePlayback_NormalizeVoiceBlockStart(float length_f,
                                            float ls,
                                            uint32_t start,
                                            bool loop_enabled,
                                            bool gate,
                                            float& pos)
{
    if(loop_enabled && gate && pos >= length_f)
        pos = ls + (pos - length_f);
    if(pos < static_cast<float>(start))
        pos = static_cast<float>(start);
}

void VoicePlayback_ClampPosPastEndWhenGateOff(float length_f, bool gate, float& pos)
{
    if(!gate && pos >= length_f && length_f > 0.0f)
        pos = length_f - 1.0f;
}

void VoicePlayback_ClampPosToLastFrameIfValid(float length_f, float& pos)
{
    if(length_f > 0.0f)
        pos = length_f - 1.0f;
}

float VoicePlayback_ClampPlayheadPos(float p, uint32_t sample_length)
{
    if(p < 0.0f)
        p = 0.0f;
    const float pmax = static_cast<float>(sample_length - 1);
    if(p > pmax)
        p = pmax;
    return p;
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
