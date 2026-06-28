#pragma once

#include "arm_math.h" // arm_cfft_instance_f32
#include <cstdint>

namespace craft {

// "refresh" — Audio Refresh: an STFT spectral-whitening exciter (decoded from
// the sonicWORX plugin). Phase 1 is the STFT skeleton only: 2048-pt window,
// 1024 hop, 50% overlap, sqrt-Hann analysis+synthesis, overlap-add, with the
// spectrum passed through UNMODIFIED (identity). Verifying that this skeleton
// reconstructs the input near-bit-transparently is the Phase 1 goal (it mirrors
// the decoded "Intensity 100 = flat impulse, COLA" result). The whitening +
// makeup math lands in Phase 2.
//
// Streaming, mono, in-place. Inherent latency of (kFftSize - kHopSize) = 1024
// samples (Latency()); the offline render trims it so the rendered sample is
// not time-shifted.
//
// Raw params p[6]: 0=Intensity, stored 0..40 = bipolar -20..+20 (0 neutral, <0
// darker, >0 brighter, at constant level), 1=linear (0..3 = No/Quarter/Half/
// Full). 2..5 reserved.

static constexpr int kFftSize = 2048;
static constexpr int kHopSize = 1024;

// Per-instance STFT working set. Large (~64 KB) so it lives in SDRAM, owned by
// the caller (CraftChain places one per slot in .sdram_bss) and bound via
// BindState. Kept POD + caller-owned so the host probe can allocate its own and
// craft_refresh.cpp stays free of memory-section attributes (host-portable).
struct RefreshState
{
    float inFifo[kFftSize];
    float frame[kFftSize];  // snapshot of inFifo at hop-fill (the spread compute reads this)
    float outFifo[kFftSize]; // two hop-halves: [0,kHopSize) and [kHopSize,2*kHopSize) (double buffer)
    float outAccum[2 * kFftSize];
    // Complex spectrum (separate re/im) the spectral stage operates on. Bound to fast
    // on-chip RAM (DTCM) by the owner via BindFftScratch.
    float* re;
    float* im;
    float win[kFftSize];          // sqrt-Hann (analysis == synthesis)
    float logf[kFftSize / 2 + 1];   // FastLog of each bin's centre frequency (const; for roll hf[])
    float logmag[kFftSize / 2 + 1]; // per-bin log-magnitude (Phase 2)
    float env[kFftSize / 2 + 1];    // scratch
    float magavg[kFftSize / 2 + 1]; // per-bin TIME-smoothed magnitude (sustain)
    float hf[kFftSize / 2 + 1];     // skirt HF-rolloff weight per bin (1..~0.18)
};

class CraftRefresh
{
  public:
    // Must be called once before Reset/Process (CraftChain binds the slot's
    // SDRAM state). Process is a no-op until state is bound.
    void BindState(RefreshState* st) { st_ = st; }
    // Bind the FFT in-place buffers to fast on-chip RAM (the strided FFT access is
    // far too slow on SDRAM). Stored here, NOT written into st_ — this is called from
    // a pre-main global constructor, and st_ points to SDRAM which isn't up until
    // hw.Init(). The pointers are applied to st_ in Reset() (which runs post-init).
    void BindFftScratch(float* re, float* im, float* cbuf)
    {
        fft_re_   = re;
        fft_im_   = im;
        fft_cbuf_ = cbuf; // interleaved complex scratch (2*kFftSize) for arm_cfft_f32
    }
    // Bind the CMSIS REAL-FFT instance to use. The owner points this at one whose
    // twiddle/bit-rev tables live in fast DTCM (the stock instance's tables are in
    // QSPI flash and exceed the D-cache, which stalled the FFT).
    void BindRfftInstance(const arm_rfft_fast_instance_f32* s) { rfft_ = s; }
    // Bind a prebuilt sqrt-Hann window (kFftSize floats). The owner builds it ONCE
    // off the audio thread; BuildTables_ then just memcpys it instead of running 2048
    // software cos calls in Reset() on the audio thread (that spiked a block and
    // clicked when fresh was engaged). nullptr -> BuildTables_ computes it (host probe).
    void BindWinSource(const float* w) { win_src_ = w; }

    void Reset(float sample_rate);
    void SetParams(const uint8_t p[6], float sample_rate);
    void Process(float* buf, uint32_t n);

    // Algorithmic latency in samples (constant once active). The render flushes
    // this many extra samples and trims the leading delay. Base STFT delay is one
    // frame (kFftSize); the FFT-spread pipeline adds one extra hop of output delay,
    // so the total is kFftSize + kHopSize. (Verified by the host impulse probe.)
    uint32_t Latency() const { return st_ ? static_cast<uint32_t>(kFftSize + kHopSize) : 0u; }

  private:
    void BuildTables_(float sr);
    void RunPhase_(); // one phase of the spread per-frame STFT (window/FFT | spectral | iFFT/OLA)

    RefreshState* st_           = nullptr;
    float*        fft_re_       = nullptr; // fast-RAM FFT scratch (applied to st_ in Reset)
    float*        fft_im_       = nullptr;
    float*        fft_cbuf_     = nullptr; // 2*kFftSize DTCM scratch for the real FFT
    const arm_rfft_fast_instance_f32* rfft_ = nullptr; // CMSIS real-FFT (DTCM-table copy)
    const float*  win_src_      = nullptr; // prebuilt sqrt-Hann window (off-thread build)
    bool          tables_ready_ = false;
    bool          avg_init_     = false; // magavg seeded by the first frame yet
    int           rover_        = kFftSize - kHopSize;
    int           phase_        = 0; // 0 = idle, 1..5 = in-flight frame compute phase
    int           phase_gap_    = 0; // countdown between phases (spaces the heavy blocks out)
    int           readSel_      = 0; // which outFifo hop-half the rover reads (0/1)
    float         target_       = 0.0f; // spectral whitening target, carried 2a -> 2b
    float         e_in_         = 0.0f; // input frame energy, carried 2b -> 2c (skirt renorm)

    // Decoded params / coefficients (recomputed in SetParams).
    int   linear_    = 0;      // 0..3 (Frequency Linearization)
    float whiten_w_  = 0.0f;   // signed whitening depth (push bins toward target)
    // Skirt stage (complex unsharp): >0 ADD inharmonic sidebands (exciter, the
    // measured kernel); <0 REDUCE sidebands around partials (de-exciter, our
    // design extension). 0 = off.
    float skirt_amt_ = 0.0f;
    // Phase 3: Frequency Linearization blend (from linear_). 0 = target is the
    // global flat level (full whitening); 1 = target follows the input's local
    // spectral envelope (preserve frequency balance, minimal broad whitening).
    float lin_beta_   = 0.0f;
    // Last applied roll value (dr, -20..20). hf[] depends only on this, so the
    // 1025-bin recompute is gated to when it changes. -1000 = sentinel "never
    // computed" so the first SetParams (and any Reset) forces a rebuild.
    int   roll_       = -1000;
};

} // namespace craft
