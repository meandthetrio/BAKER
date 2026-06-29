#pragma once

#include "craft/craft_spectral.h"
#include "craft/craft_spectral_math.h"
#include "craft/craft_zero.h" // PolarScratch
#include <cstdint>

namespace craft {

// "still" — Spectral Freeze (Spectral Toolkit; first user of Tool 2, per-bin phase
// propagation). On the live->hold edge it captures one STFT frame's magnitude and
// phase; while held it stops looking at the input and replays the captured magnitude
// every frame, advancing each bin's phase by its nominal per-hop increment so the
// frame SUSTAINS as a steady tone rather than buzzing at the frame rate. "A moment
// kept, held past its natural end" (PDF working names: Keepsake / Hold / Still).
//
// Tool 2 — nominal phase propagation: the synthesis phase of bin k advances by
// 2*pi*k*hop/N per hop. With hop = N/2 that is pi*k, i.e. exactly 0 for even bins and
// pi (sign flip) for odd bins — the phase relationship that lets a bin-centred
// sinusoid reconstruct continuously across 50%-overlap frames. Components between bin
// centres get a slightly approximate freeze (true phase-vocoder would track measured
// instantaneous frequency — a later upgrade needing extra per-bin state).
//
// Shares the per-slot PolarScratch with zero/rand (one plugin runs per slot): mag[]
// holds the captured magnitude, phase[] the running synthesis phase. No extra RAM.
class CraftFreeze : public ISpectralOp
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
        frozen_ = false;
        engine_.Reset();
    }
    void SetParams(const uint8_t p[6], float /*sample_rate*/) { hold_req_ = (p[0] != 0u); }
    void Process(float* buf, uint32_t n) { engine_.Process(buf, n); }
    uint32_t Latency() const { return engine_.Latency(); }

    int  Steps() const override { return 2; }
    void RunStep(int step, float* re, float* im, int nbins) override
    {
        if(!ps_)
            return;
        if(step == 0)
        {
            if(!hold_req_)
            {
                frozen_ = false; // live: pass the input through untouched
                return;
            }
            if(!frozen_)
            {
                // live -> hold edge: capture this frame into the shared scratch.
                PolarFwd(re, im, ps_->mag, ps_->phase, nbins);
                frozen_ = true;
            }
            return;
        }

        // step 1
        if(!frozen_)
            return; // live: passthrough (engine reconstructs the input)

        const float kPi    = 3.14159265358979f;
        const float kTwoPi = 6.28318530717959f;
        for(int k = 0; k <= nbins; ++k)
        {
            float s, c;
            FastSinCos_(ps_->phase[k], s, c);
            re[k] = ps_->mag[k] * c;
            im[k] = ps_->mag[k] * s;
            // Nominal advance for the next hop: pi for odd bins (sign flip), 0 for even.
            if(k & 1)
            {
                float p = ps_->phase[k] + kPi;
                if(p > kPi) p -= kTwoPi;
                ps_->phase[k] = p;
            }
        }
    }

  private:
    SpectralEngine engine_;
    PolarScratch*  ps_       = nullptr;
    bool           hold_req_ = false; // control-thread request (param)
    bool           frozen_   = false; // audio-thread state (captured + holding)
};

} // namespace craft
