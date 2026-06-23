#pragma once

#include <cstdint>

#include "audio_engine.h" // ProcessTptSvf

namespace craft {

// "copy" — generation-loss / resample degrade. Mono, in-place, block-based.
// Stages (in order): input drive -> transfer curve -> sample-rate reduction
// (sample-and-hold decimation) -> bit-depth crush -> tone (bandwidth LPF) ->
// wear (slow gain wobble + correlated hiss). Pitch drift is intentionally left
// to the dedicated "warp" effect; "wear" here is amplitude instability only.
//
// Runs from BOTH the audio thread (live audition) and the worker thread
// (offline render). No allocation, no globals — all state is in the instance.
//
// Raw params p[6] (see craft_params.cpp): 0=rate(enum6) 1=bits(enum5)
// 2=drive(0..100) 3=tone(enum5) 4=curve(enum4) 5=wear(0..100).
class CraftCopy
{
  public:
    void Reset(float sample_rate);
    void SetParams(const uint8_t p[6], float sample_rate);
    void Process(float* buf, uint32_t n); // in-place mono

  private:
    // Decoded params (computed in SetParams).
    float    sample_rate_   = 48000.0f;
    float    drive_gain_    = 1.0f;  // pre-curve gain
    float    out_comp_      = 1.0f;  // post makeup to tame drive
    uint8_t  curve_         = 0u;    // lin/comp/warp/noisy
    float    bit_step_      = 0.0f;  // quantization step (0 = bypass)
    float    decim_inc_     = 1.0f;  // sample-hold phase increment (1 = no reduce)
    float    tone_a1_       = 1.0f;  // SVF coeffs for tone LPF
    float    tone_a2_       = 0.0f;
    float    tone_a3_       = 0.0f;
    bool     tone_bypass_   = true;
    float    wear_amt_      = 0.0f;  // 0..1

    // Running state.
    ProcessTptSvf tone_svf_{};
    float         decim_phase_ = 0.0f;
    float         decim_held_  = 0.0f;
    float         wear_lfo_    = 0.0f; // slow random-walk gain wobble center 0
    uint32_t      rng_         = 0x1234567u;

    inline float NextRand_(); // [-1, 1)
};

} // namespace craft
