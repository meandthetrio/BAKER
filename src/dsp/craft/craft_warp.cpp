#include "craft/craft_warp.h"

#include <cmath>
#include <cstring>

namespace craft {

namespace {
constexpr float kTwoPi = 6.28318530717959f;

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float ClampHard(float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); }
} // namespace

float CraftWarp::NextRand01_()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_) * (1.0f / 4294967296.0f);
}

void CraftWarp::Reset(float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;
    std::memset(dl_, 0, sizeof(dl_));
    wpos_       = 0;
    wow_ph_     = 0.0f;
    flut_ph_    = 0.0f;
    lp_         = 0.0f;
    drop_gain_  = 1.0f;
    drop_timer_ = 0;
    rng_        = 0x9e3779b9u;
}

void CraftWarp::SetParams(const uint8_t p[6], float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;

    const float wow  = Clamp01(p[0] * 0.01f);
    const float flut = Clamp01(p[1] * 0.01f);
    const float rate = Clamp01(p[2] * 0.01f);
    drop_amt_        = Clamp01(p[3] * 0.01f);
    const float tone = Clamp01(p[4] * 0.01f);
    mix_             = Clamp01(p[5] * 0.01f);

    wow_depth_  = wow * 180.0f; // samples of slow drift
    flut_depth_ = flut * 60.0f; // samples of fast flutter

    // rate scales both LFOs: 0.25x .. 4x around their natural speeds.
    const float rate_mul = 0.25f + rate * 3.75f;
    wow_inc_             = (0.7f * rate_mul) / sample_rate_; // ~0.7 Hz wow
    flut_inc_            = (7.0f * rate_mul) / sample_rate_; // ~7 Hz flutter

    // tone = head-bump/darkening, implemented as a one-pole LPF: tone 0 ->
    // ~18 kHz (transparent), tone 1 -> ~2.7 kHz (dark).
    const float fc = 18000.0f * std::pow(2700.0f / 18000.0f, tone);
    tone_coef_     = 1.0f - std::exp(-kTwoPi * fc / sample_rate_);
    if(tone_coef_ > 1.0f)
        tone_coef_ = 1.0f;
}

void CraftWarp::Process(float* buf, uint32_t n)
{
    for(uint32_t i = 0; i < n; ++i)
    {
        const float x = buf[i];
        dl_[wpos_]    = x;

        // Wow + flutter LFOs modulate the read tap (pitch warble).
        wow_ph_ += wow_inc_;
        if(wow_ph_ >= 1.0f)
            wow_ph_ -= 1.0f;
        flut_ph_ += flut_inc_;
        if(flut_ph_ >= 1.0f)
            flut_ph_ -= 1.0f;
        const float mod = wow_depth_ * std::sin(kTwoPi * wow_ph_)
                          + flut_depth_ * std::sin(kTwoPi * flut_ph_);

        // Fractional read behind the write head by (base + mod).
        float read = static_cast<float>(wpos_) - (kBase + mod);
        while(read < 0.0f)
            read += static_cast<float>(kLen);
        const uint32_t ri  = static_cast<uint32_t>(read);
        const float    fr  = read - static_cast<float>(ri);
        const float    a   = dl_[ri & kMask];
        const float    b   = dl_[(ri + 1u) & kMask];
        float          wet = a + fr * (b - a);

        // Tape dropouts: occasional smoothed amplitude dips.
        if(drop_amt_ > 0.0f)
        {
            if(drop_timer_ == 0u && NextRand01_() < drop_amt_ * 0.0001f)
                drop_timer_ = 480u + static_cast<uint32_t>(NextRand01_() * 2400.0f); // 10-60 ms
            const float target = (drop_timer_ > 0u) ? (1.0f - drop_amt_ * 0.8f) : 1.0f;
            if(drop_timer_ > 0u)
                --drop_timer_;
            drop_gain_ += 0.002f * (target - drop_gain_);
            wet *= drop_gain_;
        }

        // Tone darkening (head-bump emulated as a gentle LPF).
        lp_ += tone_coef_ * (wet - lp_);
        wet = lp_;

        const float out = x + mix_ * (wet - x);
        buf[i]          = ClampHard(out);

        wpos_ = (wpos_ + 1u) & kMask;
    }
}

} // namespace craft
