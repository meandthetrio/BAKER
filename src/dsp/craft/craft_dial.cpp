#include "craft/craft_dial.h"

#include <cmath>

namespace craft {

namespace {
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
} // namespace

float CraftDial::Osc_(float phase) const
{
    const float s  = std::sin(kTwoPi * phase);
    const float sq = (s >= 0.0f) ? 1.0f : -1.0f;
    return s + shape_ * (sq - s); // morph sine -> square
}

void CraftDial::Reset(float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;
    tone_svf_.Reset();
    ph1_ = 0.0f;
    ph2_ = 0.0f;
}

void CraftDial::SetParams(const uint8_t p[6], float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;

    const float tune = Clamp01(p[0] * 0.01f);
    const float beat = Clamp01(p[1] * 0.01f);
    shape_           = Clamp01(p[2] * 0.01f);
    depth_           = Clamp01(p[3] * 0.01f);
    const float tone = Clamp01(p[4] * 0.01f);
    mix_             = Clamp01(p[5] * 0.01f);

    // carrier frequency: ~30 Hz .. ~3 kHz, exponential.
    const float f       = 30.0f * std::pow(100.0f, tune);
    const float beat_hz = beat * 30.0f; // 0..30 Hz detune between the two carriers
    inc1_               = f / sample_rate_;
    inc2_               = (f + beat_hz) / sample_rate_;

    // post tone LPF: ~800 Hz .. ~18 kHz, exponential. Bypass near transparent.
    const float fc = 800.0f * std::pow(22.5f, tone);
    tone_bypass_   = (fc >= 0.45f * sample_rate_);
    if(!tone_bypass_)
    {
        const float g = std::tan(kPi * fc / sample_rate_);
        const float k = 2.0f - 2.0f * 0.0f; // no resonance
        tone_a1_      = 1.0f / (1.0f + g * (g + k));
        tone_a2_      = g * tone_a1_;
        tone_a3_      = g * tone_a2_;
    }
}

void CraftDial::Process(float* buf, uint32_t n)
{
    for(uint32_t i = 0; i < n; ++i)
    {
        const float x = buf[i];

        ph1_ += inc1_;
        if(ph1_ >= 1.0f)
            ph1_ -= 1.0f;
        ph2_ += inc2_;
        if(ph2_ >= 1.0f)
            ph2_ -= 1.0f;

        // Two carriers summed -> their amplitude beats at the detune rate.
        const float car     = 0.5f * (Osc_(ph1_) + Osc_(ph2_));
        const float car_eff = (1.0f - depth_) + depth_ * car; // depth 0 -> passthrough
        float       wet     = x * car_eff;
        if(!tone_bypass_)
            wet = tone_svf_.Lp(wet, tone_a1_, tone_a2_, tone_a3_);

        float out = x + mix_ * (wet - x);
        buf[i]    = out > 1.0f ? 1.0f : (out < -1.0f ? -1.0f : out);
    }
}

} // namespace craft
