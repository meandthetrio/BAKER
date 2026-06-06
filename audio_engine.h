#pragma once
#include <cstddef>
#include <cstdint>
#include "mem_regions.h"
#include "params.h"
#include "tilt_eq.h"
#include "Effects/Reverb/DattorroReverb.h"

struct AppDiagnosticsState;

// Audio Engine Layer:
// - Reads Params::current only.
// - Hard bypass: DSP does not run when OFF.
// - Tail: when toggled OFF, keep processing with no new input until decay, then bypass.

class AudioEngine
{
  public:
    void Init(float sample_rate, size_t block_size);
    void BindDiagnostics(AppDiagnosticsState* diagnostics) { diagnostics_ = diagnostics; }

    void ProcessBlock(const float* inL,
                      const float* inR,
                      float* outL,
                      float* outR,
                      size_t size,
                      const PerformParamsCurrent& p,
                      bool sd_wav_load_busy = false);

  private:
    float  sample_rate_ = 48000.0f;
    size_t block_size_  = 48;
    AppDiagnosticsState* diagnostics_ = nullptr;

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

    // Amortized buffer clear. A full memset of the SDRAM delay lines (~384 KB)
    // inside one audio block overruns the codec and produces a click on toggle.
    // Instead we kick off `delay_clear_pending_` at tail-begin and step through
    // the ring in small chunks each ProcessBlock until done. The active tail
    // writes are zero-fed during this window, so zeroed cells stay zero.
    bool   delay_clear_pending_ = false;
    size_t delay_clear_cursor_  = 0;
    static constexpr size_t kDelayClearChunk = 4096; // ~12 blocks to clear full buf

    // When the user re-engages delay while the cursor is still clearing the
    // buffer, activation is deferred until the clear finishes. Reading dL
    // mid-clear would catch the discontinuity between old live audio and
    // tail-decay residue, write that step back into the ring via dL*fb, and
    // echo it indefinitely — exactly the "click caught in the delay" symptom.
    bool   delay_activate_pending_ = false;

    // Smoothed input-feed gain: the write into the ring used to be gated by a
    // hard `feed ? l : 0` step at the delay_on flip; one delay-time later that
    // step played back as a click at the leading edge of the first echo.
    float delay_feed_gain_ = 0.0f;

    // Smoothed wet mix used by the DSP. The function-level value steps at the
    // tail->active boundary (latched delay_tail_mix_ during tail, then jumps to
    // p.delay_mix on re-activation). Smoothing it inside the audio engine makes
    // the rejoin continuous regardless of how fast the user wiggles the fader.
    float delay_wet_mix_ = 0.0f;

    // Smoothed left/right read lengths, in samples (float so the read tap can
    // move continuously). Without this, an encoder detent on LTM/RTM steps the
    // integer read offset by tens-to-hundreds of samples in one block; the
    // read pointer jumps to a different cell whose value can be wildly
    // different from its neighbor, producing a click that gets captured into
    // the delay via dL*fb. Per-sample one-pole + linear interpolation between
    // the two adjacent cells makes the tap glide smoothly.
    float delay_len_l_smoothed_ = 0.0f;
    float delay_len_r_smoothed_ = 0.0f;

    // Per-sample one-pole coefficient: 1 / (sample_rate * time_const_sec).
    // 50 ms @ 48 kHz = 1 / 2400. Slow is intentional and cheap — the audible
    // benefit is that no fader-driven discontinuity can produce a click.
    static constexpr float kDelayFxSmoothCoeff = 1.0f / 2400.0f;

    void DelayClear_();
    void DelayClearStep_();
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
