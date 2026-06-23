#include "craft/craft_warm.h"

#include <cmath>

namespace craft {

namespace {
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float ClampHard(float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); }
inline float OnePole(float t_sec, float sr)
{
    if(t_sec <= 0.0f)
        return 1.0f;
    return 1.0f - std::exp(-1.0f / (t_sec * sr));
}
} // namespace

void CraftWarm::ComputeCoeffs_()
{
    body_lp_  = OnePole(1.0f / (6.2832f * 320.0f), sample_rate_);  // ~320 Hz low-mid
    tone_lp_  = OnePole(1.0f / (6.2832f * 1500.0f), sample_rate_); // ~1.5 kHz tilt split
    comp_atk_ = OnePole(0.005f, sample_rate_);                     // 5 ms
    comp_rel_ = OnePole(0.080f, sample_rate_);                     // 80 ms
}

void CraftWarm::Reset(float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;
    ComputeCoeffs_();
    lp_body_  = 0.0f;
    lp_tone_  = 0.0f;
    comp_env_ = 0.0f;
}

void CraftWarm::SetParams(const uint8_t p[6], float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;
    ComputeCoeffs_();

    const float drive = Clamp01(p[0] * 0.01f);
    const float tone  = Clamp01(p[1] * 0.01f);
    const float body  = Clamp01(p[2] * 0.01f);
    const float glue  = Clamp01(p[3] * 0.01f);
    const float grit  = Clamp01(p[4] * 0.01f);
    mix_              = Clamp01(p[5] * 0.01f);

    drive_gain_  = 1.0f + drive * 6.0f;
    makeup_      = 1.0f / (1.0f - 0.4f * drive); // mild loudness compensation
    bias_        = grit * 0.4f;                  // asymmetry -> even harmonics
    dc_          = std::tanh(bias_ * drive_gain_);
    body_amt_    = body * 2.0f;
    tone_tilt_   = (tone - 0.5f) * 2.0f;
    glue_ratio_  = glue * 6.0f;
    comp_makeup_ = 1.0f + glue * 0.5f;
}

void CraftWarm::Process(float* buf, uint32_t n)
{
    for(uint32_t i = 0; i < n; ++i)
    {
        const float x = buf[i];

        // Low-mid mass.
        lp_body_ += body_lp_ * (x - lp_body_);
        const float b = x + body_amt_ * lp_body_;

        // Asymmetric drive + saturation, recentred (remove the static bias DC).
        const float driven = (b + bias_) * drive_gain_;
        float       sat    = (std::tanh(driven) - dc_) * makeup_;

        // Tone tilt: bright adds highs, dark subtracts them.
        lp_tone_ += tone_lp_ * (sat - lp_tone_);
        const float high  = sat - lp_tone_;
        float       toned = sat + tone_tilt_ * kToneAmt * high;

        // Glue compression (downward, soft).
        const float a = std::fabs(toned);
        comp_env_ += (a > comp_env_ ? comp_atk_ : comp_rel_) * (a - comp_env_);
        float gr = 0.0f;
        if(comp_env_ > kCompThr)
            gr = (comp_env_ - kCompThr) * glue_ratio_;
        const float comped = toned * (1.0f / (1.0f + gr)) * comp_makeup_;

        const float out = x + mix_ * (comped - x);
        buf[i]          = ClampHard(out);
    }
}

} // namespace craft
