#pragma once

#include <cstdint>

namespace craft {

// "warp" — tape wow & flutter. A modulated delay line warbles the playback
// speed: a slow LFO (wow) plus a fast LFO (flutter) move the read tap, which
// pitch-modulates the signal. Adds tape dropouts and head-bump/darkening tone.
// Mono, in-place, block-based; runs from both the audio thread (audition) and
// worker thread (render). Self-contained delay buffer (no external memory).
//
// Raw params p[6]: 0=wow(slow depth) 1=flut(fast depth) 2=rate(mod speed)
// 3=drop(dropout amount) 4=tone(darkening) 5=mix(dry/wet). All 0..100.
class CraftWarp
{
  public:
    void Reset(float sample_rate);
    void SetParams(const uint8_t p[6], float sample_rate);
    void Process(float* buf, uint32_t n);

  private:
    static constexpr uint32_t kLen  = 512u;       // ~10.6 ms @ 48k (power of two)
    static constexpr uint32_t kMask = kLen - 1u;
    static constexpr float    kBase = 256.0f;     // base delay (samples)

    float sample_rate_ = 48000.0f;

    // Decoded params.
    float wow_depth_  = 0.0f; // samples
    float flut_depth_ = 0.0f; // samples
    float wow_inc_    = 0.0f; // cycles/sample
    float flut_inc_   = 0.0f;
    float drop_amt_   = 0.0f;
    float tone_coef_  = 1.0f; // one-pole darkening coef
    float mix_        = 1.0f;

    // Running state.
    float    dl_[kLen]   = {};
    uint32_t wpos_       = 0;
    float    wow_ph_     = 0.0f;
    float    flut_ph_    = 0.0f;
    float    lp_         = 0.0f; // tone darkening state
    float    drop_gain_  = 1.0f;
    uint32_t drop_timer_ = 0;
    uint32_t rng_        = 0x9e3779b9u;

    inline float NextRand01_(); // [0,1)
};

} // namespace craft
