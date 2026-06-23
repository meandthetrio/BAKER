#include "craft/craft_copy.h"

#include <cmath>

namespace craft {

namespace {
constexpr float kPi = 3.14159265358979f;

// Target capture rates for the rate enum {48k,32k,27k,22k,16k,12k}.
constexpr float kRateHz[6] = {48000.0f, 32000.0f, 27000.0f, 22050.0f, 16000.0f, 12000.0f};
// Effective bit depths for the bits enum {16,12,10,8,6}.
constexpr float kBits[5] = {16.0f, 12.0f, 10.0f, 8.0f, 6.0f};
// Tone LPF cutoffs for {clean,soft,ring,leak,bad}. "clean" is transparent.
constexpr float kToneHz[5] = {20000.0f, 9000.0f, 5000.0f, 3500.0f, 2000.0f};
// "ring" gets a touch of resonance for a peaky, telephone-ish bite.
constexpr float kToneRes[5] = {0.0f, 0.05f, 0.45f, 0.15f, 0.1f};

inline uint8_t ClampIdx(uint8_t v, uint8_t count)
{
    return (v < count) ? v : static_cast<uint8_t>(count - 1u);
}
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
} // namespace

float CraftCopy::NextRand_()
{
    // xorshift32 -> [-1, 1)
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (static_cast<float>(rng_) * (1.0f / 2147483648.0f)) - 1.0f;
}

void CraftCopy::Reset(float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;
    tone_svf_.Reset();
    decim_phase_ = 0.0f;
    decim_held_  = 0.0f;
    wear_lfo_    = 0.0f;
    rng_         = 0x1234567u;
}

void CraftCopy::SetParams(const uint8_t p[6], float sample_rate)
{
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;

    const uint8_t rate_idx  = ClampIdx(p[0], 6u);
    const uint8_t bits_idx  = ClampIdx(p[1], 5u);
    const float   drive     = Clamp01(p[2] * 0.01f);
    const uint8_t tone_idx  = ClampIdx(p[3], 5u);
    curve_                  = ClampIdx(p[4], 4u);
    wear_amt_               = Clamp01(p[5] * 0.01f);

    // drive: 0..100 -> 1x..~8x pre-gain, with makeup so loudness stays sane.
    drive_gain_ = 1.0f + drive * 7.0f;
    out_comp_   = 1.0f / std::pow(drive_gain_, 0.6f);

    // sample-rate reduction: hold each input sample for (sr/target) outputs.
    const float target = kRateHz[rate_idx];
    decim_inc_         = (target >= sample_rate_) ? 1.0f : (target / sample_rate_);
    if(decim_inc_ > 1.0f)
        decim_inc_ = 1.0f;

    // bit crush: quantization step over the [-1,1] range. 16 bits -> bypass.
    const float bits = kBits[bits_idx];
    bit_step_        = (bits >= 16.0f) ? 0.0f : (2.0f / std::pow(2.0f, bits));

    // tone: TPT-SVF lowpass coeffs (same math as the global process filter).
    const float fc = kToneHz[tone_idx];
    tone_bypass_   = (fc >= 0.45f * sample_rate_);
    if(!tone_bypass_)
    {
        const float g  = std::tan(kPi * fc / sample_rate_);
        const float k  = 2.0f - 2.0f * kToneRes[tone_idx];
        tone_a1_       = 1.0f / (1.0f + g * (g + k));
        tone_a2_       = g * tone_a1_;
        tone_a3_       = g * tone_a2_;
    }
}

void CraftCopy::Process(float* buf, uint32_t n)
{
    for(uint32_t i = 0; i < n; ++i)
    {
        float x = buf[i];

        // 1) input drive
        x *= drive_gain_;

        // 2) transfer curve
        switch(curve_)
        {
            case 0: // lin — clip the driven signal
                x = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
                break;
            case 1: // comp — soft compress
                x = std::tanh(x);
                break;
            case 2: // warp — asymmetric (even-harmonic) shape
                x = std::tanh(x + 0.25f * x * x);
                break;
            case 3: // noisy — soft shape + grain
                x = std::tanh(x) + 0.03f * NextRand_();
                break;
            default: break;
        }
        x *= out_comp_;

        // 3) sample-rate reduction (sample-and-hold decimation)
        if(decim_inc_ < 1.0f)
        {
            decim_phase_ += decim_inc_;
            if(decim_phase_ >= 1.0f)
            {
                decim_phase_ -= 1.0f;
                decim_held_ = x;
            }
            x = decim_held_;
        }

        // 4) bit-depth crush
        if(bit_step_ > 0.0f)
            x = std::floor(x / bit_step_ + 0.5f) * bit_step_;

        // 5) tone (bandwidth LPF)
        if(!tone_bypass_)
            x = tone_svf_.Lp(x, tone_a1_, tone_a2_, tone_a3_);

        // 6) wear — slow gain wobble + correlated hiss
        if(wear_amt_ > 0.0f)
        {
            // random walk, lightly low-passed, centred at 0.
            wear_lfo_ += 0.0008f * NextRand_();
            wear_lfo_ *= 0.999f;
            const float wob   = 1.0f + wear_amt_ * 0.25f * wear_lfo_ * 40.0f;
            const float hiss  = wear_amt_ * 0.01f * NextRand_();
            x = x * wob + hiss;
        }

        // final safety clip
        buf[i] = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
    }
}

} // namespace craft
