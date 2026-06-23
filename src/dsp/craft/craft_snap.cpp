#include "craft/craft_snap.h"

#include <cmath>

namespace craft {

namespace {
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float ClampHard(float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); }
// One-pole coefficient for a time constant `t` seconds.
inline float OnePole(float t_sec, float sr)
{
    if(t_sec <= 0.0f)
        return 1.0f;
    return 1.0f - std::exp(-1.0f / (t_sec * sr));
}
} // namespace

void CraftSnap::ComputeCoeffs_()
{
    fast_atk_ = OnePole(0.0003f, sample_rate_); // 0.3 ms — catch attacks
    fast_rel_ = OnePole(0.008f, sample_rate_);  // 8 ms
    slow_atk_ = OnePole(0.012f, sample_rate_);  // 12 ms — lags the attack
    slow_rel_ = OnePole(0.040f, sample_rate_);  // 40 ms
    lp_coef_  = OnePole(1.0f / (6.2832f * 2000.0f), sample_rate_); // ~2 kHz split
}

void CraftSnap::Reset(float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;
    ComputeCoeffs_();
    fast_env_ = 0.0f;
    slow_env_ = 0.0f;
    g_        = 0.0f;
    lp_       = 0.0f;
}

void CraftSnap::SetParams(const uint8_t p[6], float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;
    ComputeCoeffs_();

    sense_           = Clamp01(p[0] * 0.01f);
    burn_            = Clamp01(p[1] * 0.01f);
    edge_            = Clamp01(p[2] * 0.01f);
    const float dcy  = Clamp01(p[3] * 0.01f);
    clip_            = Clamp01(p[4] * 0.01f);
    mix_             = Clamp01(p[5] * 0.01f);

    // burn-envelope decay: 2 ms (click) .. 300 ms (punch/smear).
    const float decay_sec = 0.002f + dcy * 0.298f;
    g_rel_                = std::exp(-1.0f / (decay_sec * sample_rate_));
}

void CraftSnap::Process(float* buf, uint32_t n)
{
    for(uint32_t i = 0; i < n; ++i)
    {
        const float x  = buf[i];
        const float ax = std::fabs(x);

        // Dual envelope followers (attack/release one-poles).
        fast_env_ += (ax > fast_env_ ? fast_atk_ : fast_rel_) * (ax - fast_env_);
        slow_env_ += (ax > slow_env_ ? slow_atk_ : slow_rel_) * (ax - slow_env_);

        // Transient = fast rising above slow. Sensitivity scales the detector.
        float detect = fast_env_ - slow_env_;
        if(detect < 0.0f)
            detect = 0.0f;
        float trans = detect * (1.0f + sense_ * 7.0f);
        if(trans > 1.0f)
            trans = 1.0f;

        // Burn envelope: jump on a transient, decay by `decay`.
        g_ = g_ * g_rel_;
        if(trans > g_)
            g_ = trans;

        // High-freq content for the "edge" bite.
        lp_ += lp_coef_ * (x - lp_);
        const float hp = x - lp_;

        // Burn the front edge: extra drive + bite, scaled by g_.
        const float drive = 1.0f + burn_ * 8.0f * g_;
        const float d     = x * drive + edge_ * 4.0f * g_ * hp;

        // Overload shape: soft (tanh) -> hard clip.
        const float soft   = std::tanh(d);
        const float hard   = ClampHard(d);
        float       shaped = soft + clip_ * (hard - soft);

        // Gentle makeup so the burn adds bite without runaway level.
        shaped *= 1.0f / (1.0f + burn_ * 3.0f * g_);

        const float out = x + mix_ * (shaped - x);
        buf[i]          = ClampHard(out);
    }
}

} // namespace craft
