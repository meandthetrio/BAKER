#pragma once
#include <cstddef>
#include <cstdint>
#include "delay_biquad.h"
#include "mem_regions.h"
#include "params.h"
#include "Effects/Reverb/DattorroReverb.h"

// Audio Engine Layer:
// - Reads Params::current only.
// - Hard bypass: DSP does not run when OFF.
// - Tail: when toggled OFF, keep processing with no new input until decay, then bypass.

class AudioEngine
{
  public:
    void Init(float sample_rate, size_t block_size);

    void ProcessBlock(const float* inL,
                      const float* inR,
                      float* outL,
                      float* outR,
                      size_t size,
                      const PerformParamsCurrent& p);

  private:
    float  sample_rate_ = 48000.0f;
    size_t block_size_  = 48;

    // ---- SAT ----
    static inline float SoftClip(float x);

    // ---- DELAY (stereo lines + MID band-limit on tap for wet and feedback) ----
  public:
    static constexpr size_t kDelayMaxSamples = 24000; // 500ms @ 48k
    static constexpr float  kDelayTimeMaxMs  = 500.0f;
  private:
    size_t delay_wr_ = 0;

    DelayMidBiquad delay_mid_hp_[2]{};
    DelayMidBiquad delay_mid_lp_[2]{};

    bool     delay_active_  = false;
    bool     delay_tailing_ = false;
    uint32_t delay_tail_blocks_left_ = 0;
    uint16_t delay_quiet_blocks_     = 0;
    float    delay_tail_mix_         = 0.0f;

    void DelayClear_();
    void DelayProcess_(float dryL, float dryR, float mix, bool feed_input, size_t len_l,
                       size_t len_r, float fb, float& outL, float& outR);

    // ---- DATTORRO REVERB ----
    void ReverbClear_();
    void ReverbUpdateParamsDattorro_(const PerformParamsCurrent& p);
    void ReverbProcessDattorro_(float inL, float inR, bool feed_input,
                                float& wetL, float& wetR);

    DattorroReverb dattorro_;

    bool     reverb_active_  = false;
    bool     reverb_tailing_ = false;
    uint32_t reverb_tail_blocks_left_ = 0;
    uint16_t reverb_quiet_blocks_     = 0;
    float    reverb_tail_mix_         = 0.0f;

    // ---- Tail tuning ----
    static constexpr uint32_t kDelayTailMaxBlocks  = 1200; // ~1.2s max tail
    static constexpr uint32_t kReverbTailMaxBlocks = 2500; // ~2.5s max tail
    static constexpr uint16_t kQuietBlocksToStop   = 200;  // ~200ms quiet -> stop early
    static constexpr float    kTailSilenceThresh   = 1e-4f;
};
