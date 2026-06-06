#pragma once

#include "voice_engine.h"

float ComputeRatio(uint8_t note, uint8_t root_key);
float ComputeRatioFromSemitoneDelta(float semitones);
float ComputeFadeStepMs(float sample_rate, float fade_ms);

void InitEnvelope(EnvStage& stage,
                  float&    level,
                  float&    a_step,
                  float&    d_step,
                  float&    r_step,
                  float&    sustain,
                  float     attack_ms,
                  float     decay_ms,
                  float     sustain_level,
                  float     release_ms,
                  float     sample_rate);

void SetEnvelopeRelease(EnvStage& stage,
                        float&    level,
                        float&    r_step,
                        float     release_ms,
                        float     sample_rate);

// Inlined in the header so the env pre-simulation loop in RenderNormalVoice_
// (48 iterations per voice per attack block) and the per-sample fast-envelope
// path don't pay a cross-TU `bl` per iteration. Bit-identical to the prior
// out-of-line version that lived in voice_engine_playback.cpp.
static inline void StepEnvelope(EnvStage& stage,
                                float&    level,
                                float     a_step,
                                float     d_step,
                                float     sustain,
                                float     r_step)
{
    switch(stage)
    {
        case EnvStage::Attack:
            level += a_step;
            if(level >= 1.0f)
            {
                level = 1.0f;
                stage = EnvStage::Decay;
            }
            break;
        case EnvStage::Decay:
            level -= d_step;
            if(level <= sustain)
            {
                level = sustain;
                stage = EnvStage::Sustain;
            }
            break;
        case EnvStage::Sustain:
            level = sustain;
            break;
        case EnvStage::Release:
            level -= r_step;
            if(level <= 0.0f)
            {
                level = 0.0f;
                stage = EnvStage::Off;
            }
            break;
        case EnvStage::Off:
        default:
            level = 0.0f;
            break;
    }
}

float ComputeLoopBoundaryFade(float pos, uint32_t start, uint32_t end, float sample_rate);
