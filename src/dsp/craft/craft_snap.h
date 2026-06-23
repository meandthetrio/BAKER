#pragma once

#include <cstdint>

namespace craft {

// "snap" — transient burn. Detects attacks (fast vs slow envelope follower) and
// "burns" the front edge with extra drive, high-freq bite, and clipping. Sustain
// is left largely untouched. Mono, in-place, block-based; runs from both the
// audio thread (audition) and worker thread (render).
//
// Raw params p[6]: 0=sense(detect sensitivity) 1=burn(attack drive) 2=edge(bite)
// 3=decay(burn length, short=click/long=punch) 4=clip(soft->hard) 5=mix(dry/wet).
// All 0..100.
class CraftSnap
{
  public:
    void Reset(float sample_rate);
    void SetParams(const uint8_t p[6], float sample_rate);
    void Process(float* buf, uint32_t n);

  private:
    float sample_rate_ = 48000.0f;

    // Decoded params.
    float sense_   = 0.0f; // detector gain
    float burn_    = 0.0f; // drive amount
    float edge_    = 0.0f; // high-freq emphasis
    float clip_    = 0.0f; // 0 = soft (tanh), 1 = hard clip
    float mix_     = 1.0f; // dry/wet
    float g_rel_   = 0.0f; // burn-envelope release coef (from decay)

    // Fixed follower coeffs (set in Reset/SetParams from sample rate).
    float fast_atk_ = 0.0f;
    float fast_rel_ = 0.0f;
    float slow_atk_ = 0.0f;
    float slow_rel_ = 0.0f;
    float lp_coef_  = 0.0f; // one-pole for high-freq (edge) extraction

    // Running state.
    float fast_env_ = 0.0f;
    float slow_env_ = 0.0f;
    float g_        = 0.0f; // burn envelope (peak-hold + decay)
    float lp_       = 0.0f; // low-pass state for edge HP

    void ComputeCoeffs_();
};

} // namespace craft
