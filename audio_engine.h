#pragma once
#include <cstddef>
#include <cstdint>
#include "mem_regions.h"
#include "params.h"

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

    // ---- DELAY (mono feedback delay) ----
  public:
    static constexpr size_t kDelayMaxSamples = 12000; // ~250ms @ 48k (small on purpose)
  private:
    size_t delay_wr_  = 0;
    size_t delay_len_ = 6000;
    float  delay_fb_  = 0.55f;

    bool     delay_active_  = false;
    bool     delay_tailing_ = false;
    uint32_t delay_tail_blocks_left_ = 0;
    uint16_t delay_quiet_blocks_     = 0;
    float    delay_tail_mix_         = 0.0f;

    void DelayClear_();
    void DelayProcess_(float dryL, float dryR, float mix, bool feed_input,
                       float& outL, float& outR);

    // ---- PHASE A BAKER REVERB (mono-in, stereo-out 8-line late tank) ----
    struct Diffuser
    {
        float*  buf = nullptr;
        size_t  len = 0;
        size_t  idx = 0;
        float   g   = 0.5f;

        void Init(float* b, size_t l) { buf = b; len = l; idx = 0; }
        void Clear();
        float Process(float in);
    };

    struct TankLine
    {
        float*  buf = nullptr;
        size_t  len = 0;
        size_t  idx = 0;
        float   damp_z = 0.0f;

        void Init(float* b, size_t l) { buf = b; len = l; idx = 0; }
        void Clear();
        float Read() const;
        void  WriteAdvance(float in);
    };

  public:
    static constexpr size_t kReverbTankCount = 8;
    static constexpr size_t kReverbPreMaxSamples = 5761; // ~120 ms @ 48k
    static constexpr size_t kReverbDiff1Len = 149;
    static constexpr size_t kReverbDiff2Len = 211;
    static constexpr size_t kReverbDiff3Len = 293;
    static constexpr size_t kReverbTank1Len = 821;  // ~17.1 ms @ 48k
    static constexpr size_t kReverbTank2Len = 1013; // ~21.1 ms @ 48k
    static constexpr size_t kReverbTank3Len = 1249; // ~26.0 ms @ 48k
    static constexpr size_t kReverbTank4Len = 1499; // ~31.2 ms @ 48k
    static constexpr size_t kReverbTank5Len = 1783; // ~37.1 ms @ 48k
    static constexpr size_t kReverbTank6Len = 2179; // ~45.4 ms @ 48k
    static constexpr size_t kReverbTank7Len = 2591; // ~54.0 ms @ 48k
    static constexpr size_t kReverbTank8Len = 3187; // ~66.4 ms @ 48k
  private:
    Diffuser reverb_diffusers_[3];
    TankLine reverb_tank_[kReverbTankCount];
    size_t   reverb_pre_len_ = 1;
    size_t   reverb_pre_wr_  = 0;
    float    reverb_decay_gain_ = 0.78f;
    float    reverb_damp_coeff_ = 0.35f;

    void ReverbInit_();
    void ReverbClear_();
    void ReverbUpdateParams_(const PerformParamsCurrent& p);
    void ReverbProcess_(float inL, float inR, bool feed_input,
                        float& wetL, float& wetR);

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
