#include "voice_engine_render_internal.h"

#include <cmath>

bool AdvancePos(float& pos,
                int8_t& dir,
                float ratio,
                float len,
                float ls,
                float le,
                bool loop_enabled,
                bool gate,
                LoopMode mode,
                float seam_offset)
{
    if(!loop_enabled || !gate || le <= ls || le > len)
    {
        pos += ratio;
        if(pos >= len)
            return false;
        return true;
    }

    if(mode == LoopMode::Forward)
    {
        if(seam_offset < 0.0f)
            seam_offset = 0.0f;
        const float loop_span = le - ls;
        if(seam_offset >= loop_span)
            seam_offset = 0.0f;

        pos += ratio;
        if(pos >= le)
        {
            const float effective_span = loop_span - seam_offset;
            pos = ls + seam_offset + std::fmod(pos - le, effective_span);
        }
    }
    else
    {
        pos += ratio * static_cast<float>(dir);
        if(dir > 0 && pos >= le)
        {
            pos = le - (pos - le);
            dir = -1;
        }
        else if(dir < 0 && pos <= ls)
        {
            pos = ls + (ls - pos);
            dir = 1;
        }
    }
    return true;
}

bool VoiceRenderLoop_FullSampleWrapGate(const Sample* s,
                                        bool loop_enabled,
                                        bool gate)
{
    return loop_enabled && gate && s->loop_start == 0 && s->loop_end == s->length;
}

float VoiceRenderLoop_ApplyBoundaryFadeNoSeam(float s,
                                              bool loop_voice,
                                              uint32_t seam_frames,
                                              bool used_seam_xfade,
                                              float pos,
                                              uint32_t start,
                                              uint32_t end,
                                              float sample_rate)
{
    if(loop_voice && seam_frames == 0u && !used_seam_xfade)
        s *= ComputeLoopBoundaryFade(pos, start, end, sample_rate);
    return s;
}

float VoiceRenderLoop_ApplyBoundaryFadeNoSeam(float s,
                                              bool loop_voice,
                                              uint32_t seam_frames,
                                              bool used_seam_xfade,
                                              float pos,
                                              float fade_start_threshold,
                                              float fade_end_threshold,
                                              uint32_t start,
                                              uint32_t end,
                                              float sample_rate)
{
    if(!loop_voice || seam_frames != 0u || used_seam_xfade)
        return s;
    // Fast-path: positions strictly inside the loop interior have a fade
    // coefficient of 1.0 so we can skip ComputeLoopBoundaryFade entirely.
    if(pos >= fade_start_threshold && pos <= fade_end_threshold)
        return s;
    return s * ComputeLoopBoundaryFade(pos, start, end, sample_rate);
}
