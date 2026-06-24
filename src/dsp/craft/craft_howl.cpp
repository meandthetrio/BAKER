#include "craft/craft_howl.h"

#include <cmath>
#include <cstring>

namespace craft {

namespace {
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float ClampHard(float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); }
} // namespace

void CraftHowl::Reset(float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;
    std::memset(dl_, 0, sizeof(dl_));
    wpos_    = 0;
    damp_lp_ = 0.0f;
}

void CraftHowl::SetParams(const uint8_t p[6], float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;

    const float gain = Clamp01(p[0] * 0.01f);
    const float feed = Clamp01(p[1] * 0.01f);
    const float time = Clamp01(p[2] * 0.01f);
    const float fold = Clamp01(p[3] * 0.01f);
    const float damp = Clamp01(p[4] * 0.01f);
    mix_             = Clamp01(p[5] * 0.01f);

    gain_drive_ = 0.5f + gain * 1.5f;        // 0.5 .. 2.0 into the network
    feed_       = feed * 0.95f;              // bounded < 1
    dlen_       = 16.0f + time * 464.0f;     // 16..480 samples (~3 kHz .. ~100 Hz)
    fold_gain_  = 1.0f + fold * 6.0f;        // sine-fold drive
    damp_coef_  = 1.0f - damp * 0.95f;       // 1 = open, ->0.05 = heavy loop LPF
}

void CraftHowl::Process(float* buf, uint32_t n)
{
    for(uint32_t i = 0; i < n; ++i)
    {
        const float x = buf[i];

        // Fractional read `dlen_` samples behind the write head.
        float read = static_cast<float>(wpos_) - dlen_;
        while(read < 0.0f)
            read += static_cast<float>(kLen);
        const uint32_t ri    = static_cast<uint32_t>(read);
        const float    fr    = read - static_cast<float>(ri);
        const float    a     = dl_[ri & kMask];
        const float    b     = dl_[(ri + 1u) & kMask];
        const float    dread = a + fr * (b - a);

        // Darken the feedback path (stability).
        damp_lp_ += damp_coef_ * (dread - damp_lp_);
        const float fb = damp_lp_;

        // Sine wavefolder in the loop (inherently bounded, no runaway loops).
        const float folded = std::sin(fb * fold_gain_);

        // Input + feedback, then SOFT-LIMIT before writing: bounds the loop to
        // +/-1 regardless of `feed` — the runaway guard.
        const float in = std::tanh(x * gain_drive_ + feed_ * folded);
        dl_[wpos_]     = in;

        const float out = x + mix_ * (in - x);
        buf[i]          = ClampHard(out);

        wpos_ = (wpos_ + 1u) & kMask;
    }
}

} // namespace craft
