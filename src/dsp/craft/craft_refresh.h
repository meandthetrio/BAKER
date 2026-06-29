#pragma once

#include "craft/craft_spectral.h"
#include <cstdint>

namespace craft {

// "fresh" — Audio Refresh: an STFT spectral-whitening exciter (decoded from the
// sonicWORX plugin). Now a thin SPECTRAL OP on top of the shared SpectralEngine
// (Tool 0 of the Spectral Toolkit): the engine owns all framing (window / FFT /
// OLA / phase-spreading); fresh supplies only the 3-step spectral operation
// (whitening + frequency-linearization envelope + skirt). The audio is identical
// to the pre-extraction monolith — same window, FFT, OLA, and phase schedule.
//
// Raw params p[6]: 0=tilt (whitening, stored 0..40 = bipolar -20..+20), 1=linr
// (Frequency Linearization, 0..3 = No/Quarter/Half/Full), 2=skirt (0..40 bipolar),
// 3=roll (skirt HF/LF rolloff, 0..40 bipolar). 4..5 reserved.

// fresh-specific per-slot scratch (~20 KB). Lives alongside the SpectralState in
// RAM_D2 (caller-owned). The generic framing buffers are in SpectralState; these
// are the whitening pipeline's working arrays. POD + caller-owned (host-portable).
struct FreshScratch
{
    float logf[kFftSize / 2 + 1];   // FastLog of each bin's centre frequency (const; for hf[])
    float logmag[kFftSize / 2 + 1]; // per-bin log-magnitude / gain scratch
    float env[kFftSize / 2 + 1];    // local-envelope scratch
    float magavg[kFftSize / 2 + 1]; // per-bin TIME-smoothed magnitude (sustain)
    float hf[kFftSize / 2 + 1];     // skirt freq-rolloff weight per bin (1..0)
};

class CraftRefresh : public ISpectralOp
{
  public:
    // Bind the per-slot framing state + fresh scratch (both in RAM_D2). Wires this
    // object as the engine's spectral op. Process is a no-op until everything is bound.
    void BindState(SpectralState* st, FreshScratch* fs)
    {
        fs_ = fs;
        engine_.BindState(st);
        engine_.BindOp(this);
    }
    void BindFftScratch(float* re, float* im, float* cbuf) { engine_.BindFftScratch(re, im, cbuf); }
    void BindRfftInstance(const arm_rfft_fast_instance_f32* s) { engine_.BindRfftInstance(s); }
    void BindWin(const float* w) { engine_.BindWin(w); }

    void Reset(float sample_rate);
    void SetParams(const uint8_t p[6], float sample_rate);
    void Process(float* buf, uint32_t n) { engine_.Process(buf, n); }

    uint32_t Latency() const { return engine_.Latency(); }

    // ISpectralOp: the whitening pipeline, split into 3 sub-phases (whitening target
    // | envelope+gains+norm | skirt+renorm) so each stays under the per-block budget.
    int  Steps() const override { return 3; }
    void RunStep(int step, float* re, float* im, int nbins) override;

  private:
    void BuildTables_(float sr); // builds fs_->logf (and seeds fs_->hf flat)

    SpectralEngine engine_;
    FreshScratch*  fs_           = nullptr;
    bool           tables_ready_ = false;
    bool           avg_init_     = false; // magavg seeded by the first frame yet
    float          target_       = 0.0f;  // whitening target, carried step 0 -> 1
    float          e_in_         = 0.0f;  // input frame energy, carried step 1 -> 2 (skirt renorm)

    // Decoded params / coefficients (recomputed in SetParams).
    int   linear_    = 0;      // 0..3 (Frequency Linearization)
    float whiten_w_  = 0.0f;   // signed whitening depth
    float skirt_amt_ = 0.0f;   // >0 add inharmonic sidebands, <0 reduce, 0 off
    float lin_beta_  = 0.0f;   // freq-linearization blend (0 flat target .. 1 local envelope)
    int   roll_      = -1000;  // last applied roll (gates the hf[] recompute; sentinel forces first build)
};

} // namespace craft
