#pragma once

#include "arm_math.h" // arm_rfft_fast_instance_f32
#include <cstdint>

namespace craft {

// Shared STFT host for the CRAFT spectral effects (the "Spectral Toolkit", Tool 0).
// Extracted from the original CraftRefresh so every spectral effect reuses ONE
// framing engine instead of duplicating it: 2048-pt window, 1024 hop, 50% overlap,
// sqrt-Hann analysis+synthesis (product = Hann, perfect COLA), overlap-add, with a
// one-hop double-buffered output delay. Each effect supplies only its spectral-
// domain operation (ISpectralOp); the engine owns the FIFO/window/FFT/OLA and the
// per-frame work spreading.
//
// Inherent latency = kFftSize + kHopSize = 3072 samples (Latency()): one frame of
// STFT delay plus the one extra hop of output delay the spread pipeline adds. The
// offline render flushes this many extra samples and trims the leading delay.
//
// SINGLE-FRAME CONSTRAINT (inherited from the fresh design): the FFT scratch
// (re/im/cbuf) and the rfft instance are shared singletons bound by the owner, so
// only ONE spectral effect may be computing a frame at a time. A slot runs one
// plugin at a time and the preview uses one slot, so this holds in practice — do
// not run two STFT effects (e.g. fresh + thru) in different slots simultaneously.

static constexpr int kFftSize = 2048;
static constexpr int kHopSize = 1024;

// Per-instance STFT framing working set (~40 KB). Large, so it lives in RAM_D2
// (caller-owned: CraftChain places one per slot in .ram_d2_bss and binds it via
// BindState). POD + caller-owned so the host probe can allocate its own and this
// stays free of memory-section attributes (host-portable). re/im point to shared
// fast-RAM FFT scratch; win points to the shared sqrt-Hann window (NOT a per-state
// copy — saves 8 KB/slot vs. the old design).
struct SpectralState
{
    float        inFifo[kFftSize];
    float        frame[kFftSize];       // snapshot of inFifo at hop-fill (the op reads its transform)
    float        outFifo[kFftSize];     // two hop-halves: [0,kHopSize) and [kHopSize,2*kHopSize)
    float        outAccum[2 * kFftSize];
    float*       re;                    // -> shared fast-RAM FFT scratch (bound, applied in Reset)
    float*       im;
    const float* win;                   // -> shared sqrt-Hann window (bound, applied in Reset)
};

// The spectral-domain operation an effect supplies. The engine transforms a frame
// to a one-sided spectrum (re/im over kFftSize/2 + 1 bins), then drives the op
// across Steps() sub-phases — one per audio block — so a heavy op is spread out and
// no single block overruns. Identity (passthrough) op: Steps() == 1, RunStep is a
// no-op. fresh: Steps() == 3 (the original whitening / envelope / skirt phases).
class ISpectralOp
{
  public:
    virtual ~ISpectralOp() {}
    // Number of sub-phases the op needs (>= 1). Read once per frame, at frame start.
    virtual int Steps() const = 0;
    // Run sub-phase `step` (0 .. Steps()-1) over the one-sided spectrum re/im. nbins
    // = kFftSize/2 (the Nyquist bin index; usable bins are 0..nbins inclusive).
    virtual void RunStep(int step, float* re, float* im, int nbins) = 0;
};

class SpectralEngine
{
  public:
    // Bind the per-instance framing state (the owner places it in RAM_D2). Process
    // is a no-op until state + scratch + rfft + win are all bound.
    void BindState(SpectralState* st) { st_ = st; }
    // Bind the shared FFT in-place buffers (fast RAM; strided FFT access is far too
    // slow on SDRAM). Stored here, applied to st_ in Reset() — this is called from a
    // pre-main global ctor and st_ points to RAM_D2, which is fine pre-main, but the
    // pointer plumbing is kept uniform with the original design.
    void BindFftScratch(float* re, float* im, float* cbuf)
    {
        fft_re_   = re;
        fft_im_   = im;
        fft_cbuf_ = cbuf; // interleaved complex scratch (2*kFftSize) for the real FFT
    }
    // Bind the CMSIS real-FFT instance (the owner points it at one whose twiddle /
    // bit-rev tables live in fast DTCM).
    void BindRfftInstance(const arm_rfft_fast_instance_f32* s) { rfft_ = s; }
    // Bind the shared, prebuilt sqrt-Hann window (kFftSize floats, built once off the
    // audio thread). Applied to st_->win in Reset().
    void BindWin(const float* w) { win_src_ = w; }
    // Bind the effect's spectral-domain operation.
    void BindOp(ISpectralOp* op) { op_ = op; }

    void     Reset();
    void     Process(float* buf, uint32_t n);
    uint32_t Latency() const
    {
        return (st_ && rfft_) ? static_cast<uint32_t>(kFftSize + kHopSize) : 0u;
    }

  private:
    void RunPhase_(); // one phase of the spread per-frame STFT

    SpectralState*                    st_       = nullptr;
    float*                            fft_re_   = nullptr; // shared fast-RAM scratch (applied to st_ in Reset)
    float*                            fft_im_   = nullptr;
    float*                            fft_cbuf_ = nullptr; // 2*kFftSize DTCM scratch for the real FFT
    const arm_rfft_fast_instance_f32* rfft_     = nullptr;
    const float*                      win_src_  = nullptr;
    ISpectralOp*                      op_       = nullptr;

    // Phase machine. 0 = idle; 1 = window; 2 = fwd FFT; 3..(2+op_steps_) = op steps;
    // then inverse FFT; then OLA + slide. Spread one phase per Process() call, spaced
    // kPhaseGap apart; the one-hop double-buffered emit gives ~21 blocks per frame.
    int rover_     = kFftSize - kHopSize;
    int phase_     = 0;
    int phase_gap_ = 0;
    int readSel_   = 0; // which outFifo hop-half the rover reads (0/1)
    int op_steps_  = 1; // op_->Steps() snapshot for the in-flight frame
};

} // namespace craft
