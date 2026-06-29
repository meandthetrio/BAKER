#pragma once

#include "craft/craft_spectral.h"
#include <cmath>
#include <cstdint>

namespace craft {

// Number of STFT frames of history (hop 1024 @ 48k -> ~46.9 frames/s, so ~1.0 s).
static constexpr int kDelayFrames = 48;

// Tool 5 — multi-frame spectral history ring (one per slot). Big, so it lives in
// SDRAM and is accessed whole-frame / per-bin-column (mostly sequential within each
// delay band), NOT in the FFT butterfly — so it dodges the strided-SDRAM stall. ~384
// KB/slot. Caller-owned (CraftChain places it in .sdram_bss and binds it).
struct DelayRing
{
    float re[kDelayFrames][kFftSize / 2 + 1];
    float im[kDelayFrames][kFftSize / 2 + 1];
};

// "smear" — Spectral Delay (Spectral Toolkit; first user of Tool 5). Delay each
// frequency bin by an INDEPENDENT amount — lows lag, highs lead (or any tilt) — so a
// signal smears across the spectrum, "arriving at different times" (PDF working names:
// Arrival / Lag / Driftwood / Late Channel / Undertow). Each frame's spectrum is
// pushed into the history ring; the output reads each bin from a per-bin-delayed past
// frame and blends it with the present (spectral dry/wet), with optional feedback for
// regenerating tails.
//
// Per-bin delay is interpolated linearly across frequency between `low` (DC end) and
// `high` (Nyquist end) frame counts, so low>high = lows lag / highs lead (the classic
// shape) and high>low inverts it. The ring fills lazily (a frame counter gates reads
// of not-yet-written slots) so no large SDRAM memset is needed on the audio thread.
class CraftDelay : public ISpectralOp
{
  public:
    void BindState(SpectralState* st)
    {
        engine_.BindState(st);
        engine_.BindOp(this);
    }
    void BindRing(DelayRing* r) { ring_ = r; }
    void BindFftScratch(float* re, float* im, float* cbuf) { engine_.BindFftScratch(re, im, cbuf); }
    void BindRfftInstance(const arm_rfft_fast_instance_f32* s) { engine_.BindRfftInstance(s); }
    void BindWin(const float* w) { engine_.BindWin(w); }

    void Reset(float /*sample_rate*/)
    {
        writeIdx_ = 0;
        filled_   = 0; // ring treated as empty (no garbage read) until refilled
        engine_.Reset();
    }
    // p[0]=low (DC-end delay), p[1]=high (Nyquist-end delay), each 0..100 -> 0..(N-1)
    // frames; p[2]=mix (spectral dry/wet); p[3]=feed (feedback, capped < 1).
    void SetParams(const uint8_t p[6], float /*sample_rate*/)
    {
        const float maxD = static_cast<float>(kDelayFrames - 1);
        lowD_  = static_cast<int>(static_cast<float>(p[0]) * 0.01f * maxD + 0.5f);
        highD_ = static_cast<int>(static_cast<float>(p[1]) * 0.01f * maxD + 0.5f);
        mix_   = static_cast<float>(p[2]) * 0.01f;
        feed_  = static_cast<float>(p[3]) * 0.01f * 0.9f; // cap regeneration for stability
    }
    void     Process(float* buf, uint32_t n) { engine_.Process(buf, n); }
    uint32_t Latency() const { return engine_.Latency(); }

    int  Steps() const override { return 3; }
    void RunStep(int step, float* re, float* im, int nbins) override
    {
        if(!ring_)
            return;
        if(step == 0)
        {
            // Push the present frame into the ring (contiguous SDRAM row write).
            for(int k = 0; k <= nbins; ++k)
            {
                ring_->re[writeIdx_][k] = re[k];
                ring_->im[writeIdx_][k] = im[k];
            }
            return;
        }

        // Steps 1 & 2 process the spectrum in halves (keeps the per-block SDRAM read
        // load ~512 bins). step 2 also advances the ring.
        const int   k0    = (step == 1) ? 0 : (nbins / 2 + 1);
        const int   k1    = (step == 1) ? (nbins / 2) : nbins;
        const float invNb = 1.0f / static_cast<float>(nbins);
        for(int k = k0; k <= k1; ++k)
        {
            // Per-bin delay (frames), interpolated low -> high across frequency.
            float df = static_cast<float>(lowD_)
                       + static_cast<float>(highD_ - lowD_) * (static_cast<float>(k) * invNb);
            int d = static_cast<int>(df + 0.5f);
            if(d < 0) d = 0;
            else if(d > kDelayFrames - 1) d = kDelayFrames - 1;

            float dr = 0.0f, di = 0.0f;
            if(d <= filled_) // only read slots that have actually been written
            {
                int idx = writeIdx_ - d;
                if(idx < 0) idx += kDelayFrames;
                dr = ring_->re[idx][k];
                di = ring_->im[idx][k];
            }
            // Feedback: fold the delayed value back into the just-written present frame
            // so it re-delays (regenerating tail). Read before this write (above).
            if(feed_ > 0.0f)
            {
                ring_->re[writeIdx_][k] += feed_ * dr;
                ring_->im[writeIdx_][k] += feed_ * di;
            }
            // Spectral dry/wet blend.
            const float outR = (1.0f - mix_) * re[k] + mix_ * dr;
            const float outI = (1.0f - mix_) * im[k] + mix_ * di;
            re[k] = outR;
            im[k] = outI;
        }

        if(step == 2)
        {
            writeIdx_ = (writeIdx_ + 1) % kDelayFrames;
            if(filled_ < kDelayFrames - 1)
                ++filled_;
        }
    }

  private:
    SpectralEngine engine_;
    DelayRing*     ring_     = nullptr;
    int            writeIdx_ = 0;
    int            filled_   = 0; // frames written so far (saturates at kDelayFrames-1)
    int            lowD_     = 15;
    int            highD_    = 2;
    float          mix_      = 0.7f;
    float          feed_     = 0.0f;
};

} // namespace craft
