#pragma once

#include "craft/craft_spectral.h"
#include "craft/craft_spectral_math.h"
#include "craft/craft_zero.h" // PolarScratch
#include <cstdint>

namespace craft {

// "thick" — Spectral Thickening (Spectral Toolkit; first user of Tools 3 + 4, peak
// detection + fractional-bin splatter, and the first effect that ADDS spectral
// content). Detect the spectral peaks, then spray harmonic sidebands above each one
// at musical interval ratios — octave (2x), fifth (3/2), and an opt-in just major
// third (5/4) — to summon presence around what's already there: "a single voice
// becoming many" (PDF working names: Choir / Kindred / Halo / Resonance).
//
// Sideband placement uses the parabola-interpolated peak frequency (Tool 3) and the
// fractional-bin splatter (Tool 4) so the intervals land at LITERAL just frequencies
// (5:4 fused, not the equal-tempered beating third — the doc's distinctive feature).
//
// Each sideband carries a RUNNING synthesis phase advanced by its target frequency's
// true per-hop increment (pi * target_bin), so it reconstructs as a coherent tone for
// ANY interval ratio. (Scaling the wrapped source phase by the ratio only works for
// integer ratios like the octave; the fifth/third decohered — this fixes that.) The
// accumulators live in the shared PolarScratch.phase[] (thicken owns the slot and
// drives peak detection from magnitude alone, so it needs no source phase) — no extra
// memory, and it skips the atan2 entirely.
//
// Octave and fifth are "open" intervals (reinforce without committing major/minor) =
// the safe defaults; the third commits to major and clashes over minor-key material,
// so it defaults OFF (see the note in the source doc).
//
// Shares the per-slot PolarScratch (mag/phase) with the other phase-domain effects;
// the only extra state is a small per-instance peak list. No extra SDRAM/RAM_D2.

static constexpr int kThickenMaxPeaks = 64;

class CraftThicken : public ISpectralOp
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
        npeaks_ = 0;
        if(ps_) // zero the synthesis-phase accumulators (PolarScratch.phase)
            for(int k = 0; k <= kFftSize / 2; ++k)
                ps_->phase[k] = 0.0f;
        engine_.Reset();
    }
    // p[0]=oct, p[1]=fifth, p[2]=third (each 0..100 = sideband level); p[3]=thresh
    // (0..100 = peak-detection floor, higher = only the strongest peaks thickened).
    void SetParams(const uint8_t p[6], float /*sample_rate*/)
    {
        const float inv = 1.0f / 100.0f;
        octGain_   = kMaxSideband * static_cast<float>(p[0]) * inv;
        fifthGain_ = kMaxSideband * static_cast<float>(p[1]) * inv;
        thirdGain_ = kMaxSideband * static_cast<float>(p[2]) * inv;
        floorFrac_ = 0.02f + 0.28f * static_cast<float>(p[3]) * inv; // 0.02 .. 0.30
    }
    void     Process(float* buf, uint32_t n) { engine_.Process(buf, n); }
    uint32_t Latency() const { return engine_.Latency(); }

    int  Steps() const override { return 2; }
    void RunStep(int step, float* re, float* im, int nbins) override
    {
        if(!ps_)
            return;
        if(step == 0)
        {
            // Magnitude only (no atan2) + detect peaks. re/im (the carrier) and the
            // phase accumulators (ps_->phase) are left untouched.
            MagOnly(re, im, ps_->mag, nbins);
            npeaks_ = DetectPeaks(ps_->mag, nullptr, nbins, peaks_, kThickenMaxPeaks, floorFrac_);
            return;
        }
        // Splat harmonic sidebands on top of the carrier, each with its own running
        // synthesis phase (advanced by the target's true per-hop increment).
        for(int i = 0; i < npeaks_; ++i)
        {
            const SpectralPeak& p = peaks_[i];
            if(octGain_ > 0.0f)
                AddSideband_(re, im, p.bin * 2.00f, octGain_ * p.mag, nbins);
            if(fifthGain_ > 0.0f)
                AddSideband_(re, im, p.bin * 1.50f, fifthGain_ * p.mag, nbins);
            if(thirdGain_ > 0.0f)
                AddSideband_(re, im, p.bin * 1.25f, thirdGain_ * p.mag, nbins);
        }
    }

  private:
    // Advance the target bin's running synthesis phase by the target frequency's true
    // per-hop increment (2*pi*targetBin*hop/N = pi*targetBin), then splat the partial
    // there with that phase. The accumulator (ps_->phase[floor(target)]) persists
    // across frames, so the sideband reconstructs as a coherent tone at the exact
    // target frequency regardless of the interval ratio.
    void AddSideband_(float* re, float* im, float target, float mag, int nbins)
    {
        if(target < 0.0f || target > static_cast<float>(nbins))
            return;
        const int   b   = static_cast<int>(target);
        const float kPi = 3.14159265358979f;
        float       ph  = ps_->phase[b] + WrapPhase_(kPi * target);
        ph              = WrapPhase_(ph);
        ps_->phase[b]   = ph;
        SplatterAdd(re, im, target, mag, ph, nbins);
    }

    static constexpr float kMaxSideband = 0.70f; // sideband mag at 100% = 0.7 * peak

    SpectralEngine engine_;
    PolarScratch*  ps_ = nullptr;
    SpectralPeak   peaks_[kThickenMaxPeaks];
    int            npeaks_    = 0;
    float          octGain_   = 0.0f;
    float          fifthGain_ = 0.0f;
    float          thirdGain_ = 0.0f;
    float          floorFrac_ = 0.10f;
};

} // namespace craft
