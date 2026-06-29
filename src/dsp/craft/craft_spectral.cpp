#include "craft/craft_spectral.h"

#include "arm_math.h"
#include <cstring>

namespace craft {

namespace {

// Real FFT via CMSIS-DSP arm_rfft_fast_f32. The STFT input is real, so the real FFT is
// ~half the work of the full complex transform and skips the manual re/im interleave.
// The op works on a ONE-SIDED spectrum re/im[0..N/2] (N/2+1 bins); these bridge to
// CMSIS's packed real-FFT layout (cbuf[0]=DC re, cbuf[1]=Nyquist re, then
// cbuf[2k]/cbuf[2k+1]=re/im of bin k). cbuf is 2*kFftSize DTCM scratch: the transform
// runs in its DTCM half (zero-wait) rather than on the RAM_D2 re/im. arm_rfft's inverse
// already applies the 1/N scaling.
//
// Forward: real frame in re[0..N-1] -> one-sided spectrum re/im[0..N/2].
void RfftFwd_(const arm_rfft_fast_instance_f32* S, float* re, float* im, float* cbuf)
{
    const int Nb = kFftSize / 2;
    std::memcpy(cbuf, re, kFftSize * sizeof(float)); // windowed real -> DTCM
    arm_rfft_fast_f32(S, cbuf, cbuf + kFftSize, 0u); // packed spectrum in cbuf[N..2N-1]
    const float* pk = cbuf + kFftSize;
    re[0]  = pk[0]; im[0]  = 0.0f; // DC (real)
    re[Nb] = pk[1]; im[Nb] = 0.0f; // Nyquist (real)
    for(int k = 1; k < Nb; ++k) { re[k] = pk[2 * k]; im[k] = pk[2 * k + 1]; }
}
// Inverse: one-sided spectrum re/im[0..N/2] -> real time signal in re[0..N-1]. DC and
// Nyquist imaginary parts are dropped (a real signal has none); the rest map back to
// CMSIS's packed layout. The complex-conjugate half is implied by arm_rfft.
void RfftInv_(const arm_rfft_fast_instance_f32* S, float* re, float* im, float* cbuf)
{
    const int Nb = kFftSize / 2;
    cbuf[0] = re[0];  // DC re
    cbuf[1] = re[Nb]; // Nyquist re
    for(int k = 1; k < Nb; ++k) { cbuf[2 * k] = re[k]; cbuf[2 * k + 1] = im[k]; }
    arm_rfft_fast_f32(S, cbuf, cbuf + kFftSize, 1u); // real time signal in cbuf[N..2N-1]
    std::memcpy(re, cbuf + kFftSize, kFftSize * sizeof(float));
}

} // namespace

void SpectralEngine::Reset()
{
    if(!st_)
        return;
    // Point the framing state's FFT buffers + window at the bound shared scratch.
    st_->re  = fft_re_;
    st_->im  = fft_im_;
    st_->win = win_src_;
    std::memset(st_->inFifo, 0, sizeof(st_->inFifo));
    std::memset(st_->frame, 0, sizeof(st_->frame));
    std::memset(st_->outFifo, 0, sizeof(st_->outFifo));
    std::memset(st_->outAccum, 0, sizeof(st_->outAccum));
    rover_     = kFftSize - kHopSize;
    phase_     = 0;
    phase_gap_ = 0;
    readSel_   = 0;
    op_steps_  = op_ ? op_->Steps() : 1;
}

// One slice of the per-frame STFT work. Split so no single audio block does more than
// one heavy step (window | fwd-FFT | op steps | iFFT | OLA+slide) — the transform and
// its surrounding memory shuffle are separate blocks, lowering the per-block peak.
void SpectralEngine::RunPhase_()
{
    SpectralState& s  = *st_;
    const int      Nb = kFftSize / 2;

    if(phase_ == 1)
    {
        // Analysis-window the SNAPSHOT frame (frozen at hop-fill so inFifo can keep
        // accumulating during the spread compute) into the real input re[]. The
        // forward FFT is a SEPARATE phase so a single block never does both.
        for(int k = 0; k < kFftSize; ++k)
            s.re[k] = s.frame[k] * s.win[k];
        phase_ = 2;
        return;
    }

    if(phase_ == 2) // forward real FFT (windowed real frame -> one-sided spectrum)
    {
        RfftFwd_(rfft_, s.re, s.im, fft_cbuf_);
        phase_ = 3;
        return;
    }

    const int op_last = 2 + op_steps_; // last op sub-phase
    if(phase_ >= 3 && phase_ <= op_last)
    {
        if(op_)
            op_->RunStep(phase_ - 3, s.re, s.im, Nb);
        ++phase_;
        return;
    }

    if(phase_ == op_last + 1) // inverse real FFT (one-sided spectrum -> real time signal)
    {
        RfftInv_(rfft_, s.re, s.im, fft_cbuf_);
        phase_ = op_last + 2;
        return;
    }

    if(phase_ == op_last + 2) // synthesis window + overlap-add + emit + accumulator slide
    {
        for(int k = 0; k < kFftSize; ++k)
            s.outAccum[k] += s.re[k] * s.win[k];

        // Emit the finished hop into the COMPUTE half of the double-buffered output
        // (the half not currently being read); it becomes the read half at the next
        // hop-fill — the one-hop output delay that buys time to spread the FFTs.
        const int comp = readSel_ ^ 1;
        for(int k = 0; k < kHopSize; ++k)
            s.outFifo[comp * kHopSize + k] = s.outAccum[k];
        std::memmove(s.outAccum, s.outAccum + kHopSize, (2 * kFftSize - kHopSize) * sizeof(float));
        std::memset(s.outAccum + (2 * kFftSize - kHopSize), 0, kHopSize * sizeof(float));

        phase_ = 0;
        return;
    }
}

void SpectralEngine::Process(float* buf, uint32_t n)
{
    if(!st_ || !rfft_ || !st_->win)
        return;
    SpectralState& s       = *st_;
    const int      latency = kFftSize - kHopSize; // rover read offset into a hop-half

    // Spread the in-flight frame's work ACROSS the hop: (4 + op_steps_) phases, each
    // kept under the per-block budget, spaced kPhaseGap blocks apart so they don't
    // pile up. They must finish within one hop (~21 @48-sample blocks): fresh's worst
    // case is op_steps_=3 -> 7 phases, gap 1 -> 7*2=14 < 21. The hop-fill while() is
    // the safety net (and force-completes for the big-block offline render).
    constexpr int kPhaseGap = 1;
    if(phase_)
    {
        if(phase_gap_ > 0)
            --phase_gap_;
        else
        {
            RunPhase_();
            phase_gap_ = kPhaseGap;
        }
    }

    for(uint32_t i = 0; i < n; ++i)
    {
        s.inFifo[rover_] = buf[i];
        // Read from the READ half of the double-buffered output (one hop behind the
        // compute half) — this is where the extra hop of latency comes from.
        buf[i] = s.outFifo[readSel_ * kHopSize + (rover_ - latency)];
        ++rover_;
        if(rover_ >= kFftSize)
        {
            rover_ = latency;
            // Hop filled. Finish any still-pending frame compute (insurance for very
            // large blocks; a no-op for small audio blocks), publish it by swapping
            // the read/compute halves, then snapshot the new 2048-sample frame and
            // slide inFifo so it keeps accumulating.
            while(phase_)
                RunPhase_();
            readSel_ ^= 1;
            std::memcpy(s.frame, s.inFifo, kFftSize * sizeof(float));
            for(int k = 0; k < kFftSize - kHopSize; ++k)
                s.inFifo[k] = s.inFifo[k + kHopSize];
            op_steps_  = op_ ? op_->Steps() : 1; // re-read in case the op changed
            phase_     = 1;
            phase_gap_ = kPhaseGap; // delay phase 1 too, so the heavy blocks stay spaced
        }
    }
}

} // namespace craft
