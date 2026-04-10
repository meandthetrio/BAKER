#include "voice_engine_internal.h"

#include <cmath>

static constexpr float kFadeInMs = 3.0f;
static constexpr float kLoopBoundaryFadeMs = 1.0f;
static constexpr float kMinRatio = 0.25f;
static constexpr float kMaxRatio = 4.0f;

float ComputeRatio(uint8_t note, uint8_t root_key)
{
    const float semitones = static_cast<float>((int)note - (int)root_key);
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

float ComputeFadeStep(float sample_rate)
{
    return ComputeFadeStepMs(sample_rate, kFadeInMs);
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

void StepEnvelope(EnvStage& stage,
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
            pos = ls + seam_offset + (pos - le);
            while(pos >= le)
                pos = ls + seam_offset + (pos - le);
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
    const float equal_power = fade_in ? std::sqrt(mix) : std::sqrt(1.0f - mix);
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
