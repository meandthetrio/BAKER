#include "voice_engine_internal.h"

#include <cmath>

static constexpr float kMinEnvAttackMs = 2.0f;
static constexpr float kLoopBoundaryFadeMs = 1.0f;
static constexpr float kMinRatio = 0.125f;
static constexpr float kMaxRatio = 16.0f;

float ComputeRatio(uint8_t note, uint8_t root_key)
{
    const float semitones = static_cast<float>((int)note - (int)root_key);
    return ComputeRatioFromSemitoneDelta(semitones);
}

float ComputeRatioFromSemitoneDelta(float semitones)
{
    float ratio = std::pow(2.0f, semitones / 12.0f);
    if(ratio < kMinRatio)
        ratio = kMinRatio;
    if(ratio > kMaxRatio)
        ratio = kMaxRatio;
    return ratio;
}

float ComputeFadeStepMs(float sample_rate, float fade_ms)
{
    if(fade_ms < 0.0f)
        fade_ms = 0.0f;
    int fade_samples = static_cast<int>(sample_rate * 0.001f * fade_ms);
    if(fade_samples < 1)
        fade_samples = 1;
    return 1.0f / static_cast<float>(fade_samples);
}

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
                  float     sample_rate)
{
    if(attack_ms < 0.0f)
        attack_ms = 0.0f;
    if(attack_ms < kMinEnvAttackMs)
        attack_ms = kMinEnvAttackMs;
    if(decay_ms < 0.0f)
        decay_ms = 0.0f;
    if(release_ms < 0.0f)
        release_ms = 0.0f;
    if(sustain_level < 0.0f)
        sustain_level = 0.0f;
    if(sustain_level > 1.0f)
        sustain_level = 1.0f;

    int a_samps = static_cast<int>(sample_rate * 0.001f * attack_ms);
    int d_samps = static_cast<int>(sample_rate * 0.001f * decay_ms);
    int r_samps = static_cast<int>(sample_rate * 0.001f * release_ms);
    if(a_samps < 1) a_samps = 1;
    if(d_samps < 1) d_samps = 1;
    if(r_samps < 1) r_samps = 1;

    sustain = sustain_level;
    stage   = EnvStage::Attack;
    level   = 0.0f;
    a_step  = 1.0f / static_cast<float>(a_samps);
    d_step  = (1.0f - sustain) / static_cast<float>(d_samps);
    r_step  = sustain / static_cast<float>(r_samps);
    if(r_step < 1e-6f)
        r_step = 1e-6f;
}

void SetEnvelopeRelease(EnvStage& stage,
                        float&    level,
                        float&    r_step,
                        float     release_ms,
                        float     sample_rate)
{
    if(release_ms < 0.0f)
        release_ms = 0.0f;
    int r_samps = static_cast<int>(sample_rate * 0.001f * release_ms);
    if(r_samps < 1) r_samps = 1;
    stage = EnvStage::Release;
    r_step = level / static_cast<float>(r_samps);
    if(r_step < 1e-6f)
        r_step = 1e-6f;
}

// StepEnvelope is now defined inline in voice_engine_internal.h to remove the
// cross-TU call overhead from the pre-simulation loop and the per-sample
// fast-envelope path. Body intentionally moved, not duplicated.

float ComputeLoopBoundaryFade(float pos, uint32_t start, uint32_t end, float sample_rate)
{
    if(end <= start)
        return 1.0f;

    float fade_frames = sample_rate * 0.001f * kLoopBoundaryFadeMs;
    const float region_frames = static_cast<float>(end - start);
    if(fade_frames < 1.0f)
        fade_frames = 1.0f;
    if(fade_frames > region_frames * 0.5f)
        fade_frames = region_frames * 0.5f;
    if(fade_frames <= 0.0f)
        return 1.0f;

    const float start_f = static_cast<float>(start);
    const float end_f = static_cast<float>(end);
    float fade = 1.0f;
    if(pos < start_f + fade_frames)
        fade = (pos - start_f) / fade_frames;
    else if(pos > end_f - fade_frames)
        fade = (end_f - pos) / fade_frames;

    if(fade < 0.0f)
        fade = 0.0f;
    if(fade > 1.0f)
        fade = 1.0f;
    return fade;
}
