#pragma once

#include "craft/craft_spectral.h"
#include "craft/craft_spectral_math.h"
#include "craft/craft_zero.h" // PolarScratch
#include <cstdint>

namespace craft {

// "rand" — Phase Randomization (Spectral Toolkit). Keep each bin's magnitude, replace
// every phase with a fresh uniform random angle per frame: the partials decorrelate
// and a tone dissolves into a noise-like pad — "a sound losing its body and becoming
// weather" (PDF working names: Haze / Bloom / Dissolve).
//
// Same Tool 1 polar path as Phase Zeroing, with the phase set randomly instead of to
// zero. Reuses PolarScratch and shares the per-slot SpectralState + scratch with zero
// (mutually exclusive per slot), so it costs no extra RAM. A small per-instance
// xorshift32 RNG generates the phases (cheap, no libc rand()).
class CraftRand : public ISpectralOp
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

    void Reset(float /*sample_rate*/)
    {
        rng_ = 0x9E3779B9u; // fixed seed: deterministic renders
        engine_.Reset();
    }
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
                ps_->phase[k] = NextPhase_(); // scatter every partial's phase
        }
        else
        {
            PolarInv(ps_->mag, ps_->phase, re, im, nbins);
        }
    }

  private:
    // xorshift32 -> uniform phase in [-pi, pi).
    float NextPhase_()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        const float kPi = 3.14159265358979f;
        return static_cast<float>(rng_) * (2.0f * kPi / 4294967296.0f) - kPi;
    }

    SpectralEngine engine_;
    PolarScratch*  ps_  = nullptr;
    uint32_t       rng_ = 0x9E3779B9u;
};

} // namespace craft
