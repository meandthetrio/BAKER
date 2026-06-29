#include "craft/craft_refresh.h"

#include <cmath>
#include <cstring>

namespace craft {

namespace {

// --- Phase 2 tuning constants (calibrated against the reference renders) ---
constexpr float kMagEps = 1.0e-6f;
// The whitening acts on a TIME-smoothed magnitude (per-bin one-pole), so it targets
// SUSTAINED spectral peaks (tones) and ignores white noise's random per-frame
// fluctuations (whose time-average is flat). kTemporalAlpha is the per-frame
// smoothing coefficient (smaller = longer memory). Frame rate ~ 43-47 Hz (hop 1024).
constexpr float kTemporalAlpha = 0.20f;
// Whitening depth law: SIGNED, centered at the neutral setting (tilt 0 on the
// bipolar -20..+20 dial). w<0 -> darker (anti-whiten); w>0 -> brighter (whiten toward
// flat). ASYMMETRIC slopes: +20 -> ~+20 dB tilt, -20 -> ~-6 dB tilt. Level held
// constant by per-frame energy normalization at any depth.
constexpr int   kIntensityCenter = 20;      // raw value (0..40) that maps to 0
constexpr float kDarkSlope       = 0.0180f; // d<0: -20 -> w=-0.36 (~-6 dB)
constexpr float kBrightSlope     = 0.0617f; // d>0: +20 -> w~1.23 (~+20 dB)
// Per-bin gain clamps (dB) so deep spectral valleys don't blow up on lift.
constexpr float kGainMaxDb = 18.0f;
constexpr float kGainMinDb = -48.0f;
// Skirt stage = complex convolution-add: each partial gets a flat pedestal of
// inharmonic sidebands on every bin out to +-kSkirtWidth, rolling off by hf[].
// Width 4 (not 8) keeps the O(Nb*W) neighbour loop under the per-block budget.
constexpr int   kSkirtWidth  = 4;
constexpr float kSkirtAdd    = 0.02150f; // +skirt: complex conv-add amount
constexpr float kSkirtReduce = 0.02500f; // -skirt: magnitude-domain reduce fraction
// Frequency Linearization: per-step blend of the target between global flat (0) and
// the input's local envelope (1). No / Quarter / Half / Full.
constexpr float kLinBeta[4]   = {0.0f, 0.30f, 0.55f, 1.0f};
constexpr int   kEnvHalfWidth = 24; // local-envelope smoothing half-width in bins

// Fast natural log/exp. newlib-nano's logf/expf are slow software (~800 cyc each); at
// 1024 bins/frame those loops alone spiked the callback. These branchless FPU
// approximations are ~50x cheaper and accurate enough for tonal whitening (host-
// verified vs libm: log abs-err 1e-4, exp rel-err 0.02%).
inline float FastLog_(float x)
{
    union { float f; uint32_t i; } vx = {x};
    union { uint32_t i; float f; } mx = {(vx.i & 0x007FFFFFu) | 0x3F000000u};
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
    if(!fs_)
        return;
    const float fs = (sr > 1.0f) ? sr : 48000.0f;
    // Skirt frequency-weight: default FLAT (1.0). The 'roll' param recomputes it in
    // SetParams. Precompute the log of each bin's centre frequency here (CONSTANT) so
    // that recompute is just a compare + subtract + multiply per bin.
    for(int k = 0; k <= kFftSize / 2; ++k)
    {
        fs_->hf[k] = 1.0f;
        float f    = static_cast<float>(k) * fs / static_cast<float>(kFftSize);
        if(f < 1.0f) f = 1.0f;
        fs_->logf[k] = FastLog_(f);
    }
    tables_ready_ = true;
}

void CraftRefresh::Reset(float sample_rate)
{
    if(!fs_)
        return;
    if(!tables_ready_)
        BuildTables_(sample_rate > 1.0f ? sample_rate : 48000.0f);
    avg_init_ = false;
    roll_     = -1000; // force hf[] rebuild on the next SetParams
    engine_.Reset();
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
    linear_ = static_cast<int>(p[1]);
    if(linear_ < 0) linear_ = 0;
    if(linear_ > 3) linear_ = 3;
    lin_beta_ = kLinBeta[linear_];

    whiten_w_  = (dt >= 0) ? (kBrightSlope * static_cast<float>(dt)) : (kDarkSlope * static_cast<float>(dt));
    skirt_amt_ = (ds >= 0) ? (kSkirtAdd * static_cast<float>(ds)) : (kSkirtReduce * static_cast<float>(ds));

    // 'roll' (p[3]) = skirt frequency rolloff (recompute hf[]): 0 = flat; <0 = HF
    // rolloff, corner sweeps 20k->1k exponentially; >0 = LF rolloff, corner sweeps
    // 20->1k. hf[] depends ONLY on roll: GATE the 1025-bin recompute to when it
    // changes (control-rate coeff caching), computed in the log-frequency domain
    // against the precomputed per-bin log(f) (no per-bin divide or FastLog).
    int dr = static_cast<int>(p[3]) - kIntensityCenter;
    if(dr < -20) dr = -20;
    else if(dr > 20) dr = 20;
    if(fs_ && dr != roll_)
    {
        roll_ = dr;
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
            const float lf = fs_->logf[k];
            float       h  = 1.0f;
            if(dr < 0) { if(lf > logfc) h = 1.0f - (lf - logfc) * inv_span; }
            else if(dr > 0) { if(lf < logfc) h = 1.0f - (logfc - lf) * inv_span; }
            if(h < 0.0f) h = 0.0f;
            else if(h > 1.0f) h = 1.0f;
            fs_->hf[k] = h;
        }
    }
}

// The whitening pipeline, driven by the engine across 3 sub-phases. re/im are the
// one-sided spectrum (nbins = kFftSize/2 = Nyquist; usable bins 0..nbins). The
// whitening acts at CONSTANT LEVEL: bins are driven toward a flat target, then the
// frame is energy-normalized so the dials change TONE, not loudness.
void CraftRefresh::RunStep(int step, float* re, float* im, int nbins)
{
    if(!fs_)
        return;
    FreshScratch& s  = *fs_;
    const int     Nb = nbins;

    if(step == 0)
    {
        // --- Whitening target: magnitude-weighted mean of the smoothed log-mag. ---
        for(int k = 0; k <= Nb; ++k)
        {
            const float mag = std::sqrt(re[k] * re[k] + im[k] * im[k]);
            s.magavg[k]     = avg_init_ ? (s.magavg[k] + kTemporalAlpha * (mag - s.magavg[k])) : mag;
        }
        avg_init_ = true;

        double tsum = 0.0, wsum = 0.0;
        for(int k = 0; k <= Nb; ++k)
        {
            s.logmag[k]      = FastLog_(s.magavg[k] + kMagEps); // smoothed log-magnitude
            const double wgt = s.magavg[k];                     // magnitude weight
            tsum += wgt * s.logmag[k];
            wsum += wgt;
        }
        target_ = (wsum > 1e-20) ? static_cast<float>(tsum / wsum) : std::log(kMagEps);
        return;
    }

    if(step == 1)
    {
        const float target = target_;

        // Frequency Linearization: blend the per-bin target between the global flat
        // level (lin_beta_=0, full whitening) and the input's local spectral envelope
        // (lin_beta_=1, preserve frequency balance). Local envelope = box average of
        // logmag over +-kEnvHalfWidth bins (O(N) sliding window).
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

        // Whitening gain per bin (stashed into logmag[]); measure input vs post-gain
        // energy for the per-frame level normalization.
        const float gmax = std::pow(10.0f, kGainMaxDb * (1.0f / 20.0f));
        const float gmin = std::pow(10.0f, kGainMinDb * (1.0f / 20.0f));
        double      e_in = 0.0, e_out = 0.0;
        for(int k = 0; k <= Nb; ++k)
        {
            const float tk = (lin_beta_ > 0.0f) ? (target + lin_beta_ * (s.env[k] - target)) : target;
            float       g  = FastExp_(whiten_w_ * (tk - s.logmag[k]));
            if(g > gmax) g = gmax;
            else if(g < gmin) g = gmin;
            const float  mag2 = re[k] * re[k] + im[k] * im[k];
            const double mult = (k == 0 || k == Nb) ? 1.0 : 2.0; // conjugate pair
            e_in += mult * mag2;
            e_out += mult * static_cast<double>(g) * g * mag2;
            s.logmag[k] = g;
        }
        const float norm = (e_out > 1e-20) ? static_cast<float>(std::sqrt(e_in / e_out)) : 1.0f;

        for(int k = 0; k <= Nb; ++k)
        {
            const float g = s.logmag[k] * norm;
            re[k] *= g;
            im[k] *= g;
        }
        e_in_ = static_cast<float>(e_in); // carry to step 2 (skirt renorm)
        return;
    }

    // step == 2: skirt (add/reduce) + skirt renorm.
    // ADD (skirt_amt_ > 0): complex convolution-add. Each partial sprays a flat
    // pedestal of inharmonic sidebands onto its +-W neighbours, using the partial's
    // own phase (coherent), HF-rolled by hf[].
    if(skirt_amt_ > 0.0f)
    {
        for(int k = 0; k <= Nb; ++k) { s.logmag[k] = re[k]; s.env[k] = im[k]; }
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
            re[k] = s.logmag[k] + skirt_amt_ * accR;
            im[k] = s.env[k] + skirt_amt_ * accI;
        }
    }
    // REDUCE (skirt_amt_ < 0): magnitude-domain de-skirt. Scale DOWN each bin by how
    // far it sits below the nearest peak within +-W; a local peak is untouched, a deep
    // sideband hugging a peak is attenuated toward (1 - reduce). Phase preserved.
    else if(skirt_amt_ < 0.0f)
    {
        float reduce = -skirt_amt_;
        if(reduce > 1.0f) reduce = 1.0f;
        for(int k = 0; k <= Nb; ++k)
            s.logmag[k] = std::sqrt(re[k] * re[k] + im[k] * im[k]);
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
            re[k] *= gain;
            im[k] *= gain;
        }
    }

    // The skirt redistributes energy, so re-normalize back to the held level (e_in_)
    // — keeps the dial a skirt control, not a loudness one. Uniform scale.
    if(skirt_amt_ != 0.0f)
    {
        double e_after = 0.0;
        for(int k = 0; k <= Nb; ++k)
        {
            const double mult = (k == 0 || k == Nb) ? 1.0 : 2.0;
            e_after += mult * (static_cast<double>(re[k]) * re[k] + static_cast<double>(im[k]) * im[k]);
        }
        if(e_after > 1e-20)
        {
            const float rn = static_cast<float>(std::sqrt(e_in_ / e_after));
            for(int k = 0; k <= Nb; ++k) { re[k] *= rn; im[k] *= rn; }
        }
    }
}

} // namespace craft
