#pragma once

#include <cstdint>

#include "audio_engine.h" // ProcessTptSvf

namespace craft {

// "dial" — ring modulation / "between stations". Multiplies the signal by a
// carrier; a second slightly-detuned carrier produces the warbling beat you get
// tuning between two radio stations. Mono, in-place, block-based. Runs from both
// the audio thread (audition) and worker thread (render).
//
// Raw params p[6]: 0=tune(0..100 carrier freq) 1=beat(0..100 2nd-carrier detune)
// 2=shape(0..100 sine->square) 3=depth(0..100 ring amount) 4=tone(0..100 post
// LPF) 5=mix(0..100 dry/wet).
class CraftDial
{
  public:
    void Reset(float sample_rate);
    void SetParams(const uint8_t p[6], float sample_rate);
    void Process(float* buf, uint32_t n);

  private:
    float sample_rate_ = 48000.0f;
    float inc1_        = 0.0f; // carrier 1 phase increment (cycles/sample)
    float inc2_        = 0.0f; // carrier 2 (detuned) phase increment
    float shape_       = 0.0f; // 0 = sine, 1 = square
    float depth_       = 1.0f; // ring modulation amount
    float mix_         = 1.0f; // dry/wet
    float tone_a1_     = 1.0f;
    float tone_a2_     = 0.0f;
    float tone_a3_     = 0.0f;
    bool  tone_bypass_ = true;

    ProcessTptSvf tone_svf_{};
    float         ph1_ = 0.0f;
    float         ph2_ = 0.0f;

    inline float Osc_(float phase) const;
};

} // namespace craft
