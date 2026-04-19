#pragma once
#include <cstddef>
#include <cstdint>
#include "mem_regions.h"
#include "params.h"
#include "tilt_eq.h"
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

    // ---- DELAY (stereo dual delay: independent L/R tap times, per-channel feedback) ----
  public:
    static constexpr size_t kDelayMaxSamples = 48000; // 1000ms @ 48k
    static constexpr float  kDelayTimeMaxMs  = 1000.0f;
  private:
    size_t delay_wr_ = 0;

    bool     delay_active_  = false;
    bool     delay_tailing_ = false;
    uint32_t delay_tail_blocks_left_ = 0;
    uint16_t delay_quiet_blocks_     = 0;
    float    delay_tail_mix_         = 0.0f;

    void DelayClear_();
    // DelayProcess_ was inlined into ProcessDelayBlock_ (P2-Delay). The
    // per-sample helper is no longer needed; its body lives in-line inside
    // the block method so the write index can be kept in a register.

    // ---- DATTORRO REVERB ----
    void ReverbClear_();
    void ReverbUpdateParamsDattorro_(const PerformParamsCurrent& p);
    // The per-sample ReverbProcessDattorro_ wrapper was replaced by
    // DattorroReverb::ProcessBlock in P2-Dattorro; ProcessReverbBlock_ now
    // runs the block directly against dattorro_.ProcessBlock(...).

    // ---- Per-block FX processors (operate in place on L/R) ----
    // Each runs the full block through one FX stage. Called by ProcessBlock in
    // the user-configured fx_order. This preserves the stage-ordering semantics
    // of the previous per-sample switch while amortizing setup/dispatch across
    // `n` samples.
    void ProcessSatBlock_(float* L, float* R, size_t n, float pre);
    void ProcessEqBlock_(float* L, float* R, size_t n, float eq_mix);
    void ProcessDelayBlock_(float* L, float* R, size_t n,
                            const PerformParamsCurrent& p,
                            size_t len_l, size_t len_r, float fb,
                            float& wet_peak);
    void ProcessReverbBlock_(float* L, float* R, size_t n,
                             const PerformParamsCurrent& p,
                             float& wet_peak);
    void ApplyMasterBlock_(float* L, float* R, size_t n,
                           float level, float bypass_comp);

    DattorroReverb dattorro_;

    TiltEqStereo tilt_eq_{};
    bool           eq_run_prev_ = false;

    bool     reverb_active_  = false;
    bool     reverb_tailing_ = false;
    uint32_t reverb_tail_blocks_left_ = 0;
    uint16_t reverb_quiet_blocks_     = 0;
    float    reverb_tail_mix_         = 0.0f;

    // ---- Tail tuning ----
    // kQuietBlocksToStop + kTailSilenceThresh already kick the tail early when
    // the wet signal falls below ~-80 dBFS. The max-block caps are a backstop
    // for pathological cases (e.g. long-tail reverb fed to near-unity decay).
    // Tightening these saves up to 1.2s of reverb CPU and 0.6s of delay CPU
    // per bypass event. Audibly generous; revert to (900/1800/150) if the
    // tighter caps ever feel abrupt on hardware.
    static constexpr uint32_t kDelayTailMaxBlocks  = 600;  // ~0.6s max tail
    static constexpr uint32_t kReverbTailMaxBlocks = 1200; // ~1.2s max tail
    static constexpr uint16_t kQuietBlocksToStop   = 100;  // ~100ms quiet -> stop early
    static constexpr float    kTailSilenceThresh   = 1e-4f;
};
