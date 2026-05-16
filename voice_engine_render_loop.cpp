#include "voice_engine_render_internal.h"

#include <cmath>

namespace
{
void AdvanceFrameFrac(uint32_t& pos_frame, float& pos_frac, float delta)
{
    const float total = pos_frac + delta;
    const int32_t whole = static_cast<int32_t>(std::floor(total));
    const float frac = total - static_cast<float>(whole);
    const int64_t frame = static_cast<int64_t>(pos_frame) + static_cast<int64_t>(whole);

    if(frame >= 0)
    {
        pos_frame = static_cast<uint32_t>(frame);
        pos_frac = frac;
        return;
    }

    pos_frame = 0u;
    pos_frac = static_cast<float>(frame) + frac;
}

float PositionDeltaFromFrame(uint32_t pos_frame, float pos_frac, uint32_t boundary_frame)
{
    return static_cast<float>(pos_frame - boundary_frame) + pos_frac;
}

void SetPositionFromBoundaryOffset(uint32_t boundary_frame,
                                   float offset,
                                   uint32_t& pos_frame,
                                   float& pos_frac)
{
    if(offset < 0.0f)
        offset = 0.0f;
    const uint32_t whole = static_cast<uint32_t>(offset);
    pos_frame = boundary_frame + whole;
    pos_frac  = offset - static_cast<float>(whole);
}
} // namespace

bool AdvancePos(uint32_t& pos_frame,
                float& pos_frac,
                int8_t& dir,
                float ratio,
                uint32_t len,
                uint32_t ls,
                uint32_t le,
                bool loop_enabled,
                bool gate,
                LoopMode mode,
                uint32_t seam_offset)
{
    if(!loop_enabled || !gate || le <= ls || le > len)
    {
        AdvanceFrameFrac(pos_frame, pos_frac, ratio);
        if(pos_frame >= len)
            return false;
        return true;
    }

    if(mode == LoopMode::Forward)
    {
        const uint32_t loop_span = le - ls;
        if(seam_offset >= loop_span)
            seam_offset = 0u;

        AdvanceFrameFrac(pos_frame, pos_frac, ratio);
        if(pos_frame >= le)
        {
            const uint32_t effective_span = loop_span - seam_offset;
            float overshoot = PositionDeltaFromFrame(pos_frame, pos_frac, le);
            if(effective_span == 0u)
            {
                pos_frame = ls + seam_offset;
                pos_frac  = 0.0f;
            }
            else
            {
                const float span_f = static_cast<float>(effective_span);
                overshoot = std::fmod(overshoot, span_f);
                if(overshoot < 0.0f)
                    overshoot += span_f;
                SetPositionFromBoundaryOffset(ls + seam_offset,
                                              overshoot,
                                              pos_frame,
                                              pos_frac);
            }
        }
    }
    else
    {
        AdvanceFrameFrac(pos_frame, pos_frac, ratio * static_cast<float>(dir));
        if(dir > 0 && pos_frame >= le)
        {
            const float overshoot = PositionDeltaFromFrame(pos_frame, pos_frac, le);
            if(overshoot <= 0.0f)
            {
                pos_frame = le;
                pos_frac  = 0.0f;
            }
            else
            {
                float reflected = static_cast<float>(le) - overshoot;
                if(reflected < 0.0f)
                    reflected = 0.0f;
                const uint32_t whole = static_cast<uint32_t>(reflected);
                pos_frame = whole;
                pos_frac  = reflected - static_cast<float>(whole);
            }
            dir = -1;
        }
        else if(dir < 0 && (pos_frame < ls || (pos_frame == ls && pos_frac <= 0.0f)))
        {
            const float overshoot = static_cast<float>(ls) - VoicePlayback_PosAsFloat(pos_frame, pos_frac);
            if(overshoot <= 0.0f)
            {
                pos_frame = ls;
                pos_frac  = 0.0f;
            }
            else
            {
                SetPositionFromBoundaryOffset(ls, overshoot, pos_frame, pos_frac);
            }
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
                                              uint32_t pos_frame,
                                              float pos_frac,
                                              uint32_t start,
                                              uint32_t end,
                                              float sample_rate)
{
    if(loop_voice && seam_frames == 0u && !used_seam_xfade)
        s *= ComputeLoopBoundaryFade(VoicePlayback_PosAsFloat(pos_frame, pos_frac),
                                     start,
                                     end,
                                     sample_rate);
    return s;
}

float VoiceRenderLoop_ApplyBoundaryFadeNoSeam(float s,
                                              bool loop_voice,
                                              uint32_t seam_frames,
                                              bool used_seam_xfade,
                                              uint32_t pos_frame,
                                              float pos_frac,
                                              float fade_start_threshold,
                                              float fade_end_threshold,
                                              uint32_t start,
                                              uint32_t end,
                                              float sample_rate)
{
    if(!loop_voice || seam_frames != 0u || used_seam_xfade)
        return s;
    const float pos = VoicePlayback_PosAsFloat(pos_frame, pos_frac);
    // Fast-path: positions strictly inside the loop interior have a fade
    // coefficient of 1.0 so we can skip ComputeLoopBoundaryFade entirely.
    if(pos >= fade_start_threshold && pos <= fade_end_threshold)
        return s;
    return s * ComputeLoopBoundaryFade(pos, start, end, sample_rate);
}
