#pragma once

#include <cstdint>

namespace craft {

// "howl" — feedback fold. A short comb/feedback line with a wavefolder in the
// loop: metallic, resonant, can scream. STABILITY: the signal written into the
// delay each sample is tanh soft-limited, so the loop is bounded to +/-1 no
// matter how high `feed` is (feed is also capped < 1). Mono, in-place,
// block-based; runs from both the audio thread (audition) and worker (render).
//
// Raw params p[6]: 0=gain(input drive) 1=feed(feedback) 2=time(comb time/pitch)
// 3=fold(wavefold) 4=damp(loop darkening) 5=mix(dry/wet). All 0..100.
class CraftHowl
{
  public:
    void Reset(float sample_rate);
    void SetParams(const uint8_t p[6], float sample_rate);
    void Process(float* buf, uint32_t n);

  private:
    static constexpr uint32_t kLen  = 512u; // ~10.6 ms @ 48k (power of two)
    static constexpr uint32_t kMask = kLen - 1u;

    float sample_rate_ = 48000.0f;

    // Decoded params.
    float gain_drive_ = 1.0f;
    float feed_       = 0.0f; // bounded < 1
    float dlen_       = 64.0f;
    float fold_gain_  = 1.0f;
    float damp_coef_  = 1.0f; // 1 = no damping, ->0 = heavy LPF
    float mix_        = 1.0f;

    // Running state.
    float    dl_[kLen] = {};
    uint32_t wpos_     = 0;
    float    damp_lp_  = 0.0f;

};

} // namespace craft
