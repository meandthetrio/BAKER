#pragma once
#include <cstddef>
#include <cstdint>
#include "mem_regions.h"
#include "params.h"
#include "tilt_eq.h"
#include "Effects/Reverb/DattorroReverb.h"
#include "Effects/Saturator/TapeSaturator.h"

struct AppDiagnosticsState;

// Compact TPT (zero-delay-feedback) state-variable lowpass — one channel.
// Stable at any cutoff and clean under modulation, and transparent at high
// cutoff (unlike a clamped Chamberlin SVF). Coeffs (a1/a2/a3) are computed once
// per block from cutoff/resonance; Lp() runs per sample.
struct ProcessTptSvf
{
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
    void  Reset() { ic1eq = 0.0f; ic2eq = 0.0f; }
    inline float Lp(float v0, float a1, float a2, float a3)
    {
        const float v3 = v0 - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        return v2;
    }
};

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
                      bool sd_wav_load_busy = false,
                      // Velmod 2b: per-effect mono send buses (0=reverb, 1=delay,
                      // 2=sat), injected into each effect's chain slot. nullptr /
                      // sends_active=false when no voice is sending this block.
                      const float* const* send_bus = nullptr,
                      bool sends_active = false);

  private:
    float  sample_rate_ = 48000.0f;
    size_t block_size_  = 48;
    AppDiagnosticsState* diagnostics_ = nullptr;

    // ---- SAT ----
    static inline float SoftClip(float x);

    // Smoothed sat state. `pre` used to be a block-constant; one fader detent
    // stepped it by ~0.5 between blocks and the softclip output jumped with
    // it. `wet_gain` smooths the bypass<->softclip transition (softclip(L)
    // != L for non-tiny |L|, so the gate itself was an amplitude-dependent
    // step). Stage stays running until wet_gain decays to ~0 on the off side.
    float sat_pre_smoothed_    = 1.0f;
    float sat_wet_gain_        = 0.0f;
    float sat_makeup_smoothed_ = 1.0f;

    // TAPE-mode drive makeup. The tape model's pre-emphasis + (1+drive)^2 drive
    // boost raise level far more than the old softclip (which only trims -6 dB
    // internally at full drive), so the SAT fader was causing heavy peaking.
    // This attenuates the tape *wet* by a drive-proportional amount, smoothed
    // per-sample. CALIBRATION KNOB: kSatTapeMakeupDbAtFull is the dB applied at
    // drive=1 (scaled linearly with drive). Make it more negative if the SAT
    // fader still peaks, less negative if high drive gets too quiet.
    // Calibration (measured): at -18 dB, the OutFinal peak was -21 dBFS at SAT 0
    // and -13 dBFS at full SAT -> full SAT ran +8 dB hot vs dry. The makeup is
    // linear-in-drive (dB) and the output is ~all wet at full SAT, so it maps
    // ~1:1 to level there; -18 - 8 = -26 dB targets dry parity at full drive.
    // Nudge toward 0 if too quiet at full SAT, more negative if it peaks.
    // Recalibrated 2026-06-18: -26 dB over-corrected — the tape model only gains
    // ~4 dB from half->full drive (tanh flattens), so subtracting 26 dB at full
    // made full *quieter* than half (measured: full -36 dBFS, half -27 dBFS).
    // Solving from those two points: internal level is -10 dB at full / -14 dB at
    // half, so -4 dB makeup at full -> full ~-14, half ~-16: a gentle rise where
    // pushing the fader is slightly louder. Flat would be -8.
    static constexpr float kSatTapeMakeupDbAtFull = -4.0f;
    float sat_tape_makeup_smoothed_ = 1.0f;

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
    // True once the tail has produced audible wet at least once. The
    // quiet-detector only counts quiet blocks after this — otherwise a short
    // velmod send (written into the line but not yet echoed) would be wiped
    // during the pre-echo gap (up to the delay time) before it could return.
    bool     delay_tail_heard_ = false;

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
    // Velmod 2b: `send`/`send_scale` are an optional per-effect mono send
    // (nullptr = none) fed into the effect's WET generator at full level,
    // bypassing the global dry/wet fader so per-voice sends are heard even
    // with the effect's fader down.
    // `p.sat_mode` selects TAPE (0, real TapeSaturator model) vs BIT (1, the
    // legacy SoftClip path). `target_pre`/`target_makeup` only feed the BIT
    // path; `target_wet` is the bypass crossfade target for both modes.
    void ProcessSatBlock_(float* L, float* R, size_t n,
                          const PerformParamsCurrent& p,
                          float target_wet,
                          float target_pre, float target_makeup,
                          const float* send = nullptr, float send_scale = 0.0f);
    void ProcessEqBlock_(float* L, float* R, size_t n, float eq_mix);
    void ProcessDelayBlock_(float* L, float* R, size_t n,
                            const PerformParamsCurrent& p,
                            size_t len_l, size_t len_r, float fb,
                            float& wet_peak,
                            const float* send = nullptr, float send_scale = 0.0f);
    void ProcessReverbBlock_(float* L, float* R, size_t n,
                             const PerformParamsCurrent& p,
                             float& wet_peak,
                             const float* send = nullptr, float send_scale = 0.0f,
                             float wet_scale = 1.0f);
    void ApplyMasterBlock_(float* L, float* R, size_t n,
                           float level, float bypass_comp);

    DattorroReverb dattorro_;

    // TAPE-mode saturation: one mono TapeSaturator per channel (mirrors the
    // `Cuz` sat_l/sat_r layout). BIT mode still uses the inline SoftClip path.
    TapeSaturator tape_sat_l_;
    TapeSaturator tape_sat_r_;

    // Global process LPF (TPT SVF) on the summed mix. Mono: the dry voice sum is
    // collapsed to mono and filtered once before the stereo FX chain.
    ProcessTptSvf process_svf_{};

    TiltEqStereo tilt_eq_{};
    bool           eq_run_prev_ = false;

    // FX-order swap declick: a brief 0->1 cosine ramp on the post-FX bus when
    // fx_order changes between blocks. Kills the boundary click from swapping
    // the signal path mid-stream. ~2 ms at 48 kHz = 96 samples.
    uint8_t  fx_order_prev_[4]           = {0xFF, 0xFF, 0xFF, 0xFF};
    uint16_t fx_order_declick_remaining_ = 0u;
    static constexpr uint16_t kFxOrderDeclickSamples = 96u;

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
    static constexpr uint32_t kDelayTailMaxBlocks  = 3600; // ~3.6s max tail (covers
                                                           // the 1s max delay time
                                                           // plus several feedback
                                                           // repeats decaying out)
    // Reverb tail backstop. Sized to cover the longer tank decays (max feedback
    // ~0.94, see kDecayFbMax). For normal/short settings the quiet-block early
    // stop (below) ends the tail well before this. To keep the cap from ever
    // chopping a still-audible tail, the last kReverbTailFadeBlocks of the tail
    // are ramped to silence (declick) before the stage is deactivated.
    static constexpr uint32_t kReverbTailMaxBlocks = 2400; // ~2.4s max tail
    static constexpr uint32_t kReverbTailFadeBlocks = 96;  // ~96ms declick fade-out
    static constexpr uint16_t kQuietBlocksToStop   = 100;  // ~100ms quiet -> stop early
    // Delay needs a longer quiet window than reverb: feedback echoes are spaced
    // by the delay TIME (up to 1000ms), so a short window would trip during the
    // gap between echoes and clear the buffer mid-feedback (only one echo
    // heard). This must exceed the max delay time so each echo resets the
    // counter; it only runs out once the echo train has actually decayed.
    static constexpr uint16_t kDelayQuietBlocksToStop = 1200; // ~1.2s
    static constexpr float    kTailSilenceThresh   = 1e-4f;
};
