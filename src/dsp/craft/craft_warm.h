#pragma once

#include <cstdint>

namespace craft {

// "warm" — body saturation. The musical one: adds low-mid mass, asymmetric
// (even-harmonic) saturation, a dark<->bright tilt, and gentle glue compression.
// Mono, in-place, block-based; runs from both the audio thread (audition) and
// worker thread (render).
//
// Raw params p[6]: 0=drive(sat) 1=tone(dark<->bright) 2=body(low-mid mass)
// 3=glue(compression) 4=grit(asymmetry) 5=mix(dry/wet). All 0..100.
class CraftWarm
{
  public:
    void Reset(float sample_rate);
    void SetParams(const uint8_t p[6], float sample_rate);
    void Process(float* buf, uint32_t n);

  private:
    float sample_rate_ = 48000.0f;

    // Decoded params.
    float drive_gain_  = 1.0f;
    float makeup_      = 1.0f;
    float bias_        = 0.0f; // pre-sat DC offset (asymmetry)
    float dc_          = 0.0f; // saturated bias (subtracted to recentre)
    float body_amt_    = 0.0f; // low-mid boost gain
    float tone_tilt_   = 0.0f; // -1 (dark) .. +1 (bright)
    float glue_ratio_  = 0.0f; // compression strength
    float comp_makeup_ = 1.0f;
    float mix_         = 1.0f;

    // Fixed coeffs (from sample rate).
    float body_lp_ = 0.0f;
    float tone_lp_ = 0.0f;
    float comp_atk_ = 0.0f;
    float comp_rel_ = 0.0f;

    static constexpr float kCompThr = 0.30f;
    static constexpr float kToneAmt = 0.8f;

    // Running state.
    float lp_body_  = 0.0f;
    float lp_tone_  = 0.0f;
    float comp_env_ = 0.0f;

    void ComputeCoeffs_();
};

} // namespace craft
