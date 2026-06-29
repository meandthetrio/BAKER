#pragma once

#include "craft/craft_spectral.h"
#include <cstdint>

namespace craft {

// "thru" — identity STFT passthrough. Runs the full SpectralEngine (window, forward
// FFT, INVERSE FFT, overlap-add) with a do-nothing spectral op, so its output is the
// input reconstructed near bit-transparently (sqrt-Hann COLA), delayed by the engine
// latency (kFftSize + kHopSize).
//
// Purpose: a permanent CPU BASELINE for the bare STFT framing, measurable separately
// from any spectral processing. As the Spectral Toolkit's per-bin tools are built,
// each tool's marginal CPU can be read on top of this known framing floor — unlike
// fresh, whose cost also includes whitening + envelope + skirt.
//
// Shares the single-frame FFT scratch with the other spectral effects (see
// SpectralEngine): do not run two STFT effects at once.
class CraftThru : public ISpectralOp
{
  public:
    void BindState(SpectralState* st)
    {
        engine_.BindState(st);
        engine_.BindOp(this);
    }
    void BindFftScratch(float* re, float* im, float* cbuf) { engine_.BindFftScratch(re, im, cbuf); }
    void BindRfftInstance(const arm_rfft_fast_instance_f32* s) { engine_.BindRfftInstance(s); }
    void BindWin(const float* w) { engine_.BindWin(w); }

    void     Reset(float /*sample_rate*/) { engine_.Reset(); }
    void     SetParams(const uint8_t /*p*/[6], float /*sample_rate*/) {}
    void     Process(float* buf, uint32_t n) { engine_.Process(buf, n); }
    uint32_t Latency() const { return engine_.Latency(); }

    // ISpectralOp: identity — one no-op sub-phase.
    int  Steps() const override { return 1; }
    void RunStep(int /*step*/, float* /*re*/, float* /*im*/, int /*nbins*/) override {}

  private:
    SpectralEngine engine_;
};

} // namespace craft
