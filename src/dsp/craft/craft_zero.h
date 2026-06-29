#pragma once

#include "craft/craft_spectral.h"
#include "craft/craft_spectral_math.h"
#include <cstdint>

namespace craft {

// Per-slot polar working scratch for the phase-domain effects (~8 KB). Lives in
// RAM_D2 alongside the SpectralState (caller-owned). POD + host-portable.
struct PolarScratch
{
    float mag[kFftSize / 2 + 1];
    float phase[kFftSize / 2 + 1];
};

// "zero" — Phase Zeroing (Spectral Toolkit, first phase-domain effect; the first
// real client of Tool 1's polar primitives). Keep each bin's magnitude, set every
// phase to 0 so all partials are aligned at a common reference each frame: a
// robotic, metallic monotone (PDF working names: Totem / Monolith / Lockstep).
//
// Two engine sub-phases: (0) decompose to mag/phase + zero the phase, (1) recompose.
// Routing through the FULL polar round-trip (rather than the mag-only shortcut) is
// deliberate — it exercises Tool 1 end to end and gives the true on-device cost of a
// polar decompose+recompose, which phase randomization and freeze will both pay.
class CraftZero : public ISpectralOp
{
  public:
    void BindState(SpectralState* st, PolarScratch* ps)
    {
        ps_ = ps;
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

    int  Steps() const override { return 2; }
    void RunStep(int step, float* re, float* im, int nbins) override
    {
        if(!ps_)
            return;
        if(step == 0)
        {
            PolarFwd(re, im, ps_->mag, ps_->phase, nbins);
            for(int k = 0; k <= nbins; ++k)
                ps_->phase[k] = 0.0f; // align every partial to phase 0
        }
        else
        {
            PolarInv(ps_->mag, ps_->phase, re, im, nbins);
        }
    }

  private:
    SpectralEngine engine_;
    PolarScratch*  ps_ = nullptr;
};

} // namespace craft
