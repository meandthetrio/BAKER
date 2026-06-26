#include "craft/craft_refresh.h"

#include <cmath>
#include <cstring>

namespace craft {

namespace {

// In-place iterative radix-2 FFT. wcos/wsin are length n/2 twiddle tables with
// wcos[k]=cos(2*pi*k/n), wsin[k]=sin(2*pi*k/n). inverse divides by n. n must be
// a power of two. Offline render -> simplicity over split-radix speed.
void Fft(float* re, float* im, int n, bool inverse, const float* wcos, const float* wsin)
{
    // Bit-reversal permutation.
    for(int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for(; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if(i < j)
        {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }

    for(int len = 2; len <= n; len <<= 1)
    {
        const int half = len >> 1;
        const int step = n / len;
        for(int i = 0; i < n; i += len)
        {
            int k = 0;
            for(int j = 0; j < half; ++j, k += step)
            {
                const float wc = wcos[k];
                const float ws = inverse ? wsin[k] : -wsin[k];
                const int   a  = i + j;
                const int   b  = a + half;
                const float tr = re[b] * wc - im[b] * ws;
                const float ti = re[b] * ws + im[b] * wc;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }

    if(inverse)
    {
        const float inv = 1.0f / static_cast<float>(n);
        for(int i = 0; i < n; ++i)
        {
            re[i] *= inv;
            im[i] *= inv;
        }
    }
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
// +-kSkirtWidth (+-172 Hz), rolling off ~-15 dB by 8 kHz (RefreshState.hf[]).
// skirt_amt_ > 0 ADDS that skirt (exciter); < 0 SUBTRACTS the neighbour spread
// (de-skirt: sidebands hugging a partial get cancelled, purifying it).
constexpr int   kSkirtWidth  = 8;
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

} // namespace

void CraftRefresh::BuildTables_(float sr)
{
    if(!st_)
        return;
    (void)sr;
    // Skirt frequency-weight: default FLAT (1.0). The 'roll' param recomputes it
    // in SetParams (HF rolloff for roll<0, LF rolloff for roll>0).
    for(int k = 0; k <= kFftSize / 2; ++k)
        st_->hf[k] = 1.0f;
    const float twopi = 6.28318530717958647692f;
    // sqrt-Hann: window applied at BOTH analysis and synthesis, so the per-frame
    // product is Hann, which sums to unity at 50% overlap (perfect COLA).
    for(int i = 0; i < kFftSize; ++i)
    {
        const float hann = 0.5f * (1.0f - std::cos(twopi * i / static_cast<float>(kFftSize)));
        st_->win[i]      = std::sqrt(hann);
    }
    for(int k = 0; k < kFftSize / 2; ++k)
    {
        st_->wcos[k] = std::cos(twopi * k / static_cast<float>(kFftSize));
        st_->wsin[k] = std::sin(twopi * k / static_cast<float>(kFftSize));
    }
    tables_ready_ = true;
}

void CraftRefresh::Reset(float sample_rate)
{
    if(!st_)
        return;
    if(!tables_ready_)
        BuildTables_(sample_rate > 1.0f ? sample_rate : 48000.0f);
    std::memset(st_->inFifo, 0, sizeof(st_->inFifo));
    std::memset(st_->outFifo, 0, sizeof(st_->outFifo));
    std::memset(st_->outAccum, 0, sizeof(st_->outAccum));
    rover_    = kFftSize - kHopSize;
    avg_init_ = false;
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
    if(st_)
    {
        int dr = static_cast<int>(p[3]) - kIntensityCenter;
        if(dr < -20) dr = -20;
        else if(dr > 20) dr = 20;
        const float sr = (sample_rate > 1.0f) ? sample_rate : 48000.0f;
        for(int k = 0; k <= kFftSize / 2; ++k)
        {
            float f = static_cast<float>(k) * sr / static_cast<float>(kFftSize);
            if(f < 1.0f) f = 1.0f;
            float h = 1.0f;
            if(dr < 0) // HF rolloff
            {
                const float fc = 20000.0f * std::pow(1000.0f / 20000.0f, static_cast<float>(-dr) / 20.0f);
                if(f > fc)
                    h = 1.0f - std::log(f / fc) / std::log(20000.0f / fc);
            }
            else if(dr > 0) // LF rolloff
            {
                const float fc = 20.0f * std::pow(1000.0f / 20.0f, static_cast<float>(dr) / 20.0f);
                if(f < fc)
                    h = 1.0f - std::log(fc / f) / std::log(fc / 20.0f);
            }
            if(h < 0.0f) h = 0.0f;
            else if(h > 1.0f) h = 1.0f;
            st_->hf[k] = h;
        }
    }
}

void CraftRefresh::ProcessFrame_()
{
    RefreshState& s = *st_;

    // Analysis: window the current L-sample frame into the FFT real buffer.
    for(int k = 0; k < kFftSize; ++k)
    {
        s.re[k] = s.inFifo[k] * s.win[k];
        s.im[k] = 0.0f;
    }

    Fft(s.re, s.im, kFftSize, false, s.wcos, s.wsin);

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
        s.logmag[k] = std::log(s.magavg[k] + kMagEps); // smoothed log-magnitude
        const double wgt = s.magavg[k];                // magnitude weight
        tsum += wgt * s.logmag[k];
        wsum += wgt;
    }
    const float target = (wsum > 1e-20) ? static_cast<float>(tsum / wsum)
                                        : std::log(kMagEps); // silence: no-op target

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
        float       g  = std::exp(whiten_w_ * (tk - s.logmag[k]));
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
        if(k > 0 && k < Nb) // mirror onto conjugate bin
        {
            s.re[kFftSize - k] *= g;
            s.im[kFftSize - k] *= g;
        }
    }

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
            const float mr = s.logmag[k] + skirt_amt_ * accR;
            const float mi = s.env[k] + skirt_amt_ * accI;
            s.re[k] = mr;
            s.im[k] = mi;
            if(k > 0 && k < Nb) { s.re[kFftSize - k] = mr; s.im[kFftSize - k] = -mi; }
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
            if(k > 0 && k < Nb) { s.re[kFftSize - k] *= gain; s.im[kFftSize - k] *= gain; }
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
            const float rn = static_cast<float>(std::sqrt(e_in / e_after));
            for(int k = 0; k < kFftSize; ++k) { s.re[k] *= rn; s.im[k] *= rn; }
        }
    }

    Fft(s.re, s.im, kFftSize, true, s.wcos, s.wsin);

    // Synthesis window + overlap-add.
    for(int k = 0; k < kFftSize; ++k)
        s.outAccum[k] += s.re[k] * s.win[k];

    // Emit the next hop of finished output, then slide the accumulator down.
    for(int k = 0; k < kHopSize; ++k)
        s.outFifo[k] = s.outAccum[k];
    std::memmove(s.outAccum, s.outAccum + kHopSize, (2 * kFftSize - kHopSize) * sizeof(float));
    std::memset(s.outAccum + (2 * kFftSize - kHopSize), 0, kHopSize * sizeof(float));

    // Slide the input FIFO down by one hop, keeping the (L-H) overlap tail.
    for(int k = 0; k < kFftSize - kHopSize; ++k)
        s.inFifo[k] = s.inFifo[k + kHopSize];
}

void CraftRefresh::Process(float* buf, uint32_t n)
{
    if(!st_ || !tables_ready_)
        return;
    RefreshState& s        = *st_;
    const int     latency  = kFftSize - kHopSize;

    for(uint32_t i = 0; i < n; ++i)
    {
        s.inFifo[rover_] = buf[i];
        buf[i]           = s.outFifo[rover_ - latency];
        ++rover_;
        if(rover_ >= kFftSize)
        {
            rover_ = latency;
            ProcessFrame_();
        }
    }
}

} // namespace craft
