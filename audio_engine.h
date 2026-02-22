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
    float  delay_buf_[kDelayMaxSamples];
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

    // ---- SIMPLE REVERB (Schroeder-ish: 4 comb + 2 allpass per channel) ----
    struct Comb
    {
        float*  buf = nullptr;
        size_t  len = 0;
        size_t  idx = 0;
        float   fb  = 0.8f;

        void Init(float* b, size_t l) { buf = b; len = l; idx = 0; }
        void Clear();
        float Process(float in);
    };

    struct Allpass
    {
        float*  buf = nullptr;
        size_t  len = 0;
        size_t  idx = 0;
        float   g   = 0.5f;

        void Init(float* b, size_t l) { buf = b; len = l; idx = 0; }
        void Clear();
        float Process(float in);
    };

    // Tuned-ish delay lengths (small, SRAM-friendly). These are samples @ 48k.
  public:
    static constexpr size_t kC1L = 1116, kC2L = 1188, kC3L = 1277, kC4L = 1356;
    static constexpr size_t kC1R = 1139, kC2R = 1211, kC3R = 1300, kC4R = 1379;
    static constexpr size_t kA1L = 225,  kA2L = 341;
    static constexpr size_t kA1R = 248,  kA2R = 364;
  private:
    float comb1L_[kC1L], comb2L_[kC2L], comb3L_[kC3L], comb4L_[kC4L];
    float comb1R_[kC1R], comb2R_[kC2R], comb3R_[kC3R], comb4R_[kC4R];
    float ap1L_[kA1L], ap2L_[kA2L];
    float ap1R_[kA1R], ap2R_[kA2R];

    Comb    combL_[4];
    Comb    combR_[4];
    Allpass apL_[2];
    Allpass apR_[2];

    float reverb_fb_ = 0.82f; // tail length control
    void  ReverbInit_();
    void  ReverbClear_();
    void  ReverbProcess_(float inL, float inR, bool feed_input,
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
