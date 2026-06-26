#pragma once

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
    float outFifo[kFftSize];
    float outAccum[2 * kFftSize];
    float re[kFftSize];
    float im[kFftSize];
    float win[kFftSize];          // sqrt-Hann (analysis == synthesis)
    float wcos[kFftSize / 2];     // FFT twiddles
    float wsin[kFftSize / 2];
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

    void Reset(float sample_rate);
    void SetParams(const uint8_t p[6], float sample_rate);
    void Process(float* buf, uint32_t n);

    // Algorithmic latency in samples (constant once active). The render flushes
    // this many extra samples and trims the leading delay. Measured end-to-end
    // delay is one full frame (kFftSize), confirmed by the host impulse probe.
    uint32_t Latency() const { return st_ ? static_cast<uint32_t>(kFftSize) : 0u; }

  private:
    void BuildTables_(float sr);
    void ProcessFrame_(); // window -> FFT -> (identity) -> iFFT -> window -> OLA

    RefreshState* st_           = nullptr;
    bool          tables_ready_ = false;
    bool          avg_init_     = false; // magavg seeded by the first frame yet
    int           rover_        = kFftSize - kHopSize;

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
};

} // namespace craft
