#include "craft/craft_refresh.h"

#include "arm_math.h"
#include "arm_const_structs.h"
#include <cmath>
#include <cstring>

namespace craft {

namespace {

// Real FFT via CMSIS-DSP arm_rfft_fast_f32. The STFT input is real, so the real FFT is
// ~half the work of the full complex transform and skips the manual re/im interleave.
// The spectral stage works on a ONE-SIDED spectrum re/im[0..N/2] (N/2+1 bins); these
// bridge to CMSIS's packed real-FFT layout (cbuf[0]=DC re, cbuf[1]=Nyquist re, then
// cbuf[2k]/cbuf[2k+1]=re/im of bin k). cbuf is 2*kFftSize DTCM scratch: the transform
// runs in its DTCM half (zero-wait) rather than on the RAM_D2 re/im. arm_rfft's inverse
// already applies the 1/N scaling (host-verified vs the old cfft path).
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

// --- Phase 2 tuning constants (calibrated against the reference renders) ---
constexpr float kMagEps       = 1.0e-6f;
// The whitening acts on a TIME-smoothed magnitude (per-bin one-pole), so it
// targets SUSTAINED spectral peaks (tones) and ignores white noise's random
// per-frame fluctuations (whose time-average is flat). kTemporalAlpha is the
// per-frame smoothing coefficient (smaller = longer memory / more "sustain"
// required). Frame rate ~ 43-47 Hz (hop 1024).
constexpr float kTemporalAlpha = 0.20f;
// Whitening depth law: SIGNED, centered at the neutral setting (Intensity 0 on
// the bipolar -20..+20 dial). w<0 -> darker (anti-whiten, emphasize peaks / cut
// floor); w>0 -> brighter (whiten toward flat). ASYMMETRIC slopes (per design):
//   +20 -> ~+20 dB tilt (kBrightSlope), -20 -> ~-6 dB tilt (kDarkSlope, the
//   measured musical dark range; anti-whitening past this sounds muddy).
// Level is held constant by per-frame energy normalization at any depth.
constexpr int   kIntensityCenter = 20;     // raw value (0..40) that maps to 0
constexpr float kDarkSlope        = 0.0180f; // d<0: -20 -> w=-0.36 (~-6 dB)
constexpr float kBrightSlope      = 0.0617f; // d>0: +20 -> w~1.23 (~+20 dB)
// Per-bin gain clamps (dB) so deep spectral valleys don't blow up on lift.
constexpr float kGainMaxDb    = 18.0f;
constexpr float kGainMinDb    = -48.0f;
// Skirt stage = complex convolution-add (data-matched to ar_kernA): each partial
// gets a FLAT, ~-60 dB pedestal of inharmonic sidebands on EVERY bin out to
// +-kSkirtWidth (+-86 Hz), rolling off ~-15 dB by 8 kHz (RefreshState.hf[]).
// skirt_amt_ > 0 ADDS that skirt (exciter); < 0 SUBTRACTS the neighbour spread
// (de-skirt: sidebands hugging a partial get cancelled, purifying it).
// Width is 4 (not 8): this is the O(Nb*W) per-bin neighbour loop that dominates
// the skirt phase. At 8 it pushed that phase to ~89% of the audio block (the live-
// audition knob-turn spike); 4 halves it to ~50%, under the FFT phases' ceiling,
// and narrows the sidebands from +-172 to +-86 Hz.
constexpr int   kSkirtWidth  = 4;
// +skirt: at dial +20 the skirt sits ~-6 dB below the carrier (user-set, far
// hotter than the measured -60 dB -- a very strong/gritty exciter). -skirt: the
// magnitude-domain reduce fraction (ear-tuned, no data below 0).
constexpr float kSkirtAdd    = 0.02150f;   // +skirt: complex conv-add amount
constexpr float kSkirtReduce = 0.02500f;   // -skirt: -20 -> reduce ~0.50 (capped to stay clean)
// Phase 3 Frequency Linearization: per-step blend of the target between global
// flat (0) and the input's local envelope (1). No / Quarter / Half / Full.
constexpr float kLinBeta[4]   = {0.0f, 0.30f, 0.55f, 1.0f};
// Local-envelope smoothing half-width in bins (~23 Hz/bin) for the Full end.
constexpr int   kEnvHalfWidth = 24;

// Fast natural log/exp. newlib-nano's logf/expf are slow software (~800 cyc each);
// at 1024 bins/frame those loops alone spiked the callback. These branchless FPU
// approximations are ~50x cheaper and accurate enough for tonal whitening (host-
// verified vs libm: log abs-err 1e-4, exp rel-err 0.02%). log = Mineiro fastlog2
// form; exp = 2^(x*log2e) split into 2^n (bit-built) * cubic poly over the fraction.
inline float FastLog_(float x)
{
    union { float f; uint32_t i; } vx = { x };
    union { uint32_t i; float f; } mx = { (vx.i & 0x007FFFFFu) | 0x3F000000u };
    const float y    = static_cast<float>(vx.i) * 1.1920928955078125e-7f;
    const float log2 = y - 124.22551499f - 1.498030302f * mx.f - 1.72587999f / (0.3520887068f + mx.f);
    return log2 * 0.69314718f;
}
inline float FastExp_(float x)
{
    const float t  = x * 1.44269504f; // log2(e)
    const float fl = std::floor(t);
    const float f  = t - fl;
    const float p  = 1.0f + f * (0.6960656f + f * (0.2253374f + f * 0.0782f)); // 2^f
    int         n  = static_cast<int>(fl);
    if(n < -126) n = -126;
    else if(n > 127) n = 127;
    union { float f; uint32_t i; } v;
    v.i = static_cast<uint32_t>(n + 127) << 23; // 2^n
    return v.f * p;
}

} // namespace

void CraftRefresh::BuildTables_(float sr)
{
    if(!st_)
        return;
    const float fs = (sr > 1.0f) ? sr : 48000.0f;
    // Skirt frequency-weight: default FLAT (1.0). The 'roll' param recomputes it
    // in SetParams (HF rolloff for roll<0, LF rolloff for roll>0). Precompute the
    // log of each bin's centre frequency here (CONSTANT) so that recompute is just a
    // compare + subtract + multiply per bin — no per-bin divide or FastLog (those made
    // a fast roll scroll an audible CPU spike on top of the skirt phase).
    for(int k = 0; k <= kFftSize / 2; ++k)
    {
        st_->hf[k] = 1.0f;
        float f    = static_cast<float>(k) * fs / static_cast<float>(kFftSize);
        if(f < 1.0f) f = 1.0f;
        st_->logf[k] = FastLog_(f);
    }
    // sqrt-Hann: window applied at BOTH analysis and synthesis, so the per-frame
    // product is Hann, which sums to unity at 50% overlap (perfect COLA). Prefer the
    // prebuilt shared table (bound by the owner, built once off the audio thread) so
    // this never runs 2048 software cos calls on the audio thread when fresh is
    // engaged (that overran a block and clicked). Fallback computes it (host probe).
    if(win_src_)
    {
        std::memcpy(st_->win, win_src_, kFftSize * sizeof(float));
    }
    else
    {
        const float twopi = 6.28318530717958647692f;
        for(int i = 0; i < kFftSize; ++i)
        {
            const float hann = 0.5f * (1.0f - std::cos(twopi * i / static_cast<float>(kFftSize)));
            st_->win[i]      = std::sqrt(hann);
        }
    }
    // (No twiddle tables to build — CMSIS arm_cfft_f32 carries its own.)
    tables_ready_ = true;
}

void CraftRefresh::Reset(float sample_rate)
{
    if(!st_)
        return;
    if(!tables_ready_)
        BuildTables_(sample_rate > 1.0f ? sample_rate : 48000.0f);
    // Point the SDRAM state's FFT buffers at the bound fast-RAM scratch. Done HERE
    // (not in BindFftScratch) because this runs post-hw.Init when SDRAM is alive.
    st_->re = fft_re_;
    st_->im = fft_im_;
    std::memset(st_->inFifo, 0, sizeof(st_->inFifo));
    std::memset(st_->frame, 0, sizeof(st_->frame));
    std::memset(st_->outFifo, 0, sizeof(st_->outFifo));
    std::memset(st_->outAccum, 0, sizeof(st_->outAccum));
    rover_     = kFftSize - kHopSize;
    phase_     = 0;
    phase_gap_ = 0;
    readSel_   = 0;
    avg_init_  = false;
    roll_      = -1000; // force hf[] rebuild on the next SetParams
}

void CraftRefresh::SetParams(const uint8_t p[6], float sample_rate)
{
    (void)sample_rate;
    // Two independent bipolar dials, each raw 0..40 -> -20..+20 (0 = neutral):
    //   p[0] = tilt (whitening),  p[2] = skirt.
    int dt = static_cast<int>(p[0]) - kIntensityCenter;
    if(dt < -20) dt = -20;
    else if(dt > 20) dt = 20;
    int ds = static_cast<int>(p[2]) - kIntensityCenter;
    if(ds < -20) ds = -20;
    else if(ds > 20) ds = 20;
    linear_   = static_cast<int>(p[1]);
    if(linear_ < 0) linear_ = 0;
    if(linear_ > 3) linear_ = 3;
    lin_beta_ = kLinBeta[linear_];

    // Asymmetric signed whitening depth (see kBrightSlope/kDarkSlope). Level is
    // held constant by per-frame energy normalization, so this is purely tonal.
    whiten_w_ = (dt >= 0) ? (kBrightSlope * static_cast<float>(dt))
                          : (kDarkSlope * static_cast<float>(dt));
    // Skirt: +ds adds sidebands (data-matched exciter), -ds reduces them.
    skirt_amt_ = (ds >= 0) ? (kSkirtAdd * static_cast<float>(ds))
                           : (kSkirtReduce * static_cast<float>(ds));

    // 'roll' (p[3]) = skirt frequency rolloff (recompute hf[]): 0 = flat; <0 = HF
    // rolloff, corner sweeps 20k->1k exponentially; >0 = LF rolloff, corner sweeps
    // 20->1k. Skirt fades linearly in log-freq from the corner to the 20k/20Hz
    // edge (0 there at full +-20). Only weights the skirt, nothing else.
    //
    // hf[] depends ONLY on roll (dr): GATE the 1025-bin recompute to when dr actually
    // changes (control-rate coeff caching). The whole curve is computed in the LOG-
    // frequency domain against the precomputed per-bin log(f) (st_->logf): the corner
    // and span are FastLog'd once, then each bin is just a compare + subtract + multiply
    // (no per-bin divide, no per-bin FastLog). That keeps a fast roll scroll cheap so it
    // no longer tips the skirt-phase block over budget.
    int dr = static_cast<int>(p[3]) - kIntensityCenter;
    if(dr < -20) dr = -20;
    else if(dr > 20) dr = 20;
    if(st_ && dr != roll_)
    {
        roll_ = dr;
        // Corner (log) + 1/log(span) are loop-invariant -> compute once.
        float logfc = 0.0f, inv_span = 0.0f;
        if(dr < 0) // HF rolloff: corner sweeps 20k->1k; skirt fades 0 from corner to 20k
        {
            const float fc = 20000.0f * std::pow(1000.0f / 20000.0f, static_cast<float>(-dr) / 20.0f);
            logfc          = FastLog_(fc);
            inv_span       = 1.0f / (FastLog_(20000.0f) - logfc);
        }
        else if(dr > 0) // LF rolloff: corner sweeps 20->1k; skirt fades 0 from corner to 20
        {
            const float fc = 20.0f * std::pow(1000.0f / 20.0f, static_cast<float>(dr) / 20.0f);
            logfc          = FastLog_(fc);
            inv_span       = 1.0f / (logfc - FastLog_(20.0f));
        }
        for(int k = 0; k <= kFftSize / 2; ++k)
        {
            const float lf = st_->logf[k];
            float       h  = 1.0f;
            if(dr < 0) { if(lf > logfc) h = 1.0f - (lf - logfc) * inv_span; }
            else if(dr > 0) { if(lf < logfc) h = 1.0f - (logfc - lf) * inv_span; }
            if(h < 0.0f) h = 0.0f;
            else if(h > 1.0f) h = 1.0f;
            st_->hf[k] = h;
        }
    }
}

// One slice of the per-frame STFT work. Split into 7 phases so no single audio block
// does more than one heavy step (window | fwd-FFT | spectral 2a | 2b | skirt | iFFT |
// OLA+slide) — the transform and its surrounding memory shuffle are separate blocks,
// which lowers the per-block peak. Driven one phase per Process() call; a one-hop
// output delay (double-buffered emit + readSel_) gives ~21 blocks to finish a frame.
void CraftRefresh::RunPhase_()
{
    RefreshState& s = *st_;

    if(phase_ == 1)
    {
        // Phase 1: analysis-window the SNAPSHOT frame (s.frame, frozen at hop-fill so
        // inFifo can keep accumulating during the spread compute) into the real input
        // re[]. The forward FFT is a SEPARATE phase (2) so a single block never does
        // the window AND the transform — that combined block was the voice ceiling.
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

    if(phase_ == 3)
    {
    // --- Phase 2/3: spectral whitening at CONSTANT LEVEL (sampler departure). ---
    // Each bin is driven toward a flat target T by whiten_w_ (SIGNED: <100 darker,
    // >100 brighter), based on the TIME-smoothed magnitude (magavg) so white-noise
    // fluctuation and one-frame transients aren't whitened. The frame is then
    // ENERGY-NORMALIZED to its input loudness, so Intensity changes TONE, not
    // level -- the original plugin's ~+-13 dB makeup/excitation ride is removed on
    // purpose (it made the effect impossible to audition). Gain applies to the
    // instantaneous re/im (phase preserved); bins 0..N/2 + conjugate mirror.
    const int Nb = kFftSize / 2; // 1024 (Nyquist); usable bins 0..Nb

    for(int k = 0; k <= Nb; ++k)
    {
        const float mag = std::sqrt(s.re[k] * s.re[k] + s.im[k] * s.im[k]);
        s.magavg[k]     = avg_init_
                              ? (s.magavg[k] + kTemporalAlpha * (mag - s.magavg[k]))
                              : mag; // seed on first frame
    }
    avg_init_ = true;

    // Flat target T = MAGNITUDE-WEIGHTED mean of the smoothed log-magnitude, so
    // silent/near-silent bins don't drag T down.
    double tsum = 0.0, wsum = 0.0;
    for(int k = 0; k <= Nb; ++k)
    {
        s.logmag[k] = FastLog_(s.magavg[k] + kMagEps); // smoothed log-magnitude
        const double wgt = s.magavg[k];                // magnitude weight
        tsum += wgt * s.logmag[k];
        wsum += wgt;
    }
    const float target = (wsum > 1e-20) ? static_cast<float>(tsum / wsum)
                                        : std::log(kMagEps); // silence: no-op target
        target_ = target; // carry to sub-phase 2b
        phase_  = 4;
        return;
    }

    if(phase_ == 4) // spectral 2b: envelope + whitening gains + per-frame norm + apply
    {
        const int   Nb     = kFftSize / 2;
        const float target = target_;

    // Phase 3: Frequency Linearization. Blend the per-bin target between the
    // global flat level (lin_beta_ = 0, full whitening) and the input's local
    // spectral envelope (lin_beta_ = 1, preserve frequency balance). The local
    // envelope = box average of logmag over +-kEnvHalfWidth bins (O(N) window).
    if(lin_beta_ > 0.0f)
    {
        double    sum = 0.0;
        int       lo = 0, hi = -1;
        const int w = kEnvHalfWidth;
        for(int k = 0; k <= Nb; ++k)
        {
            const int want_hi = (k + w > Nb) ? Nb : (k + w);
            const int want_lo = (k - w < 0) ? 0 : (k - w);
            while(hi < want_hi) { ++hi; sum += s.logmag[hi]; }
            while(lo < want_lo) { sum -= s.logmag[lo]; ++lo; }
            s.env[k] = static_cast<float>(sum / static_cast<double>(hi - lo + 1));
        }
    }

    // Whitening gain per bin (stashed into logmag[], no longer needed); measure
    // input vs post-gain energy for the per-frame level normalization.
    const float gmax = std::pow(10.0f, kGainMaxDb * (1.0f / 20.0f));
    const float gmin = std::pow(10.0f, kGainMinDb * (1.0f / 20.0f));
    double      e_in = 0.0, e_out = 0.0;
    for(int k = 0; k <= Nb; ++k)
    {
        const float tk = (lin_beta_ > 0.0f) ? (target + lin_beta_ * (s.env[k] - target)) : target;
        float       g  = FastExp_(whiten_w_ * (tk - s.logmag[k]));
        if(g > gmax) g = gmax;
        else if(g < gmin) g = gmin;
        const float  mag2 = s.re[k] * s.re[k] + s.im[k] * s.im[k];
        const double mult = (k == 0 || k == Nb) ? 1.0 : 2.0; // conjugate pair
        e_in += mult * mag2;
        e_out += mult * static_cast<double>(g) * g * mag2;
        s.logmag[k] = g;
    }
    // Per-frame energy normalization: output loudness == input loudness.
    const float norm = (e_out > 1e-20) ? static_cast<float>(std::sqrt(e_in / e_out)) : 1.0f;

    for(int k = 0; k <= Nb; ++k)
    {
        const float g = s.logmag[k] * norm;
        s.re[k] *= g;
        s.im[k] *= g;
    }
        e_in_  = static_cast<float>(e_in); // carry to sub-phase 2c (skirt renorm)
        phase_ = 5;
        return;
    }

    if(phase_ == 5) // spectral 2c: skirt (add/reduce) + skirt renorm
    {
        const int Nb = kFftSize / 2;

    // --- Skirt stage --------------------------------------------------------
    // ADD (skirt_amt_ > 0): complex convolution-add. Each partial sprays a flat
    // ~-60 dB pedestal of inharmonic sidebands onto its +-W neighbours, using the
    // partial's own phase (coherent), HF-rolled by hf[]. Data-matched to ar_kernA.
    if(skirt_amt_ > 0.0f)
    {
        for(int k = 0; k <= Nb; ++k) { s.logmag[k] = s.re[k]; s.env[k] = s.im[k]; }
        for(int k = 0; k <= Nb; ++k)
        {
            float accR = 0.0f, accI = 0.0f;
            for(int m = 1; m <= kSkirtWidth; ++m)
            {
                const int lo = (k - m < 0) ? 0 : (k - m);
                const int hi = (k + m > Nb) ? Nb : (k + m);
                accR += s.hf[lo] * s.logmag[lo] + s.hf[hi] * s.logmag[hi];
                accI += s.hf[lo] * s.env[lo] + s.hf[hi] * s.env[hi];
            }
            s.re[k] = s.logmag[k] + skirt_amt_ * accR;
            s.im[k] = s.env[k] + skirt_amt_ * accI;
        }
    }
    // REDUCE (skirt_amt_ < 0): magnitude-domain de-skirt. A complex subtract can't
    // cancel sidebands (uncorrelated phase), so instead scale DOWN each bin by how
    // far it sits below the nearest peak within +-W (its "skirt-ness"): a bin that
    // IS the local peak is untouched; a deep sideband hugging a peak is attenuated
    // toward (1 - reduce). Phase preserved; the distant floor is left alone because
    // its local max is itself.
    else if(skirt_amt_ < 0.0f)
    {
        float reduce = -skirt_amt_;
        if(reduce > 1.0f) reduce = 1.0f;
        for(int k = 0; k <= Nb; ++k)
            s.logmag[k] = std::sqrt(s.re[k] * s.re[k] + s.im[k] * s.im[k]);
        for(int k = 0; k <= Nb; ++k)
        {
            float lmax = s.logmag[k];
            for(int m = 1; m <= kSkirtWidth; ++m)
            {
                const int lo = (k - m < 0) ? 0 : (k - m);
                const int hi = (k + m > Nb) ? Nb : (k + m);
                if(s.logmag[lo] > lmax) lmax = s.logmag[lo];
                if(s.logmag[hi] > lmax) lmax = s.logmag[hi];
            }
            const float ratio = s.logmag[k] / (lmax + kMagEps);
            const float gain  = 1.0f - reduce * (1.0f - ratio);
            s.re[k] *= gain;
            s.im[k] *= gain;
        }
    }

    // The skirt redistributes energy (a strong +skirt spreads/drops the carrier),
    // so re-normalize back to the held level (e_in) — keeps the dial purely a
    // skirt control, not a loudness one. Uniform scale, so the skirt/carrier
    // ratio is unchanged.
    if(skirt_amt_ != 0.0f)
    {
        double e_after = 0.0;
        for(int k = 0; k <= Nb; ++k)
        {
            const double mult = (k == 0 || k == Nb) ? 1.0 : 2.0;
            e_after += mult * (static_cast<double>(s.re[k]) * s.re[k] + static_cast<double>(s.im[k]) * s.im[k]);
        }
        if(e_after > 1e-20)
        {
            const float rn = static_cast<float>(std::sqrt(e_in_ / e_after));
            for(int k = 0; k <= Nb; ++k) { s.re[k] *= rn; s.im[k] *= rn; }
        }
    }

        phase_ = 6;
        return;
    }

    if(phase_ == 6) // inverse real FFT (one-sided spectrum -> real time signal)
    {
        RfftInv_(rfft_, s.re, s.im, fft_cbuf_);
        phase_ = 7;
        return;
    }

    if(phase_ == 7) // synthesis window + overlap-add + emit + accumulator slide
    {
        // Synthesis window + overlap-add. (Split from the iFFT so a single block
        // never does the transform AND the OLA + 12 KB accumulator slide.)
        for(int k = 0; k < kFftSize; ++k)
            s.outAccum[k] += s.re[k] * s.win[k];

        // Emit the finished hop into the COMPUTE half of the double-buffered output
        // (the half not currently being read); it becomes the read half at the next
        // hop-fill — the one-hop output delay that buys time to spread the FFTs.
        // Slide the accumulator. (inFifo is snapshotted + slid in Process.)
        const int comp = readSel_ ^ 1;
        for(int k = 0; k < kHopSize; ++k)
            s.outFifo[comp * kHopSize + k] = s.outAccum[k];
        std::memmove(s.outAccum, s.outAccum + kHopSize, (2 * kFftSize - kHopSize) * sizeof(float));
        std::memset(s.outAccum + (2 * kFftSize - kHopSize), 0, kHopSize * sizeof(float));

        phase_ = 0;
        return;
    }
}

void CraftRefresh::Process(float* buf, uint32_t n)
{
    if(!st_ || !tables_ready_)
        return;
    RefreshState& s       = *st_;
    const int     latency = kFftSize - kHopSize; // rover read offset into a hop-half

    // Spread the in-flight frame's work ACROSS the hop: 7 phases (window | fwd-FFT |
    // spectral 2a | 2b | skirt | iFFT | OLA+slide), each kept under the per-block
    // budget, spaced ~kPhaseGap blocks apart so they don't pile up. 7 phases every
    // (kPhaseGap+1) calls must finish within one hop (~21 @48-sample blocks): with gap
    // 1, 7*(1+1)=14 < 21. (Gap 2 would be 7*3=21, no margin before the hop-fill while()
    // force-completes and stacks phases.) The hop-fill while() is the safety net (and
    // force-completes for the big-block offline render).
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
            // large blocks; a no-op for small audio blocks where 3 phases << 1 hop),
            // publish it by swapping the read/compute halves, then snapshot the new
            // 2048-sample frame and slide inFifo so it keeps accumulating.
            while(phase_)
                RunPhase_();
            readSel_ ^= 1;
            std::memcpy(s.frame, s.inFifo, kFftSize * sizeof(float));
            for(int k = 0; k < kFftSize - kHopSize; ++k)
                s.inFifo[k] = s.inFifo[k + kHopSize];
            phase_     = 1;
            phase_gap_ = kPhaseGap; // delay phase 1 too, so the heavy blocks stay spaced
        }
    }
}

} // namespace craft
