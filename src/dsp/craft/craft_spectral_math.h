#pragma once

#include <cmath>

// Spectral Toolkit — Tool 1: polar (magnitude/phase) decompose & recompose, with fast
// trig. The STFT engine works in rectangular re/im; the phase-domain effects (phase
// zeroing, phase randomization, spectral freeze) need polar form, and recomposition
// needs sin/cos. newlib's atan2f/sinf/cosf are slow software (~hundreds of cycles);
// at 1025 bins/frame that dominates. These branchless FPU approximations mirror the
// FastLog_/FastExp_ approach already used in fresh.
//
// Accuracy (host-verified vs libm over the full range):
//   FastAtan2_  max abs err ~3e-5 rad   (degree-11 odd minimax of atan over [0,1])
//   FastSinCos_ max abs err ~9e-4
// Plenty for phase manipulation; the polar round-trip reconstructs at < -70 dB.

namespace craft {

// atan2 approximation. atan over the reduced ratio [0,1] is a degree-11 odd minimax
// (~3e-5 rad), then reflected/quadrant-corrected. All four quadrants; handles x==y==0.
inline float FastAtan2_(float y, float x)
{
    const float kPi     = 3.14159265358979f;
    const float kHalfPi = 1.57079632679490f;
    const float ax      = std::fabs(x);
    const float ay      = std::fabs(y);
    const float mx      = (ax > ay) ? ax : ay;
    const float mn      = (ax > ay) ? ay : ax;
    if(mx == 0.0f)
        return 0.0f;
    const float a = mn / mx; // ratio in [0,1]
    const float s = a * a;
    float       r = ((((-0.01172120f * s + 0.05265332f) * s - 0.11643287f) * s + 0.19354346f) * s
                - 0.33262347f)
                  * s
                + 0.99997726f;
    r *= a;
    if(ay > ax) r = kHalfPi - r; // reflect across the diagonal
    if(x < 0.0f) r = kPi - r;    // quadrants II/III
    if(y < 0.0f) r = -r;         // quadrants III/IV
    return r;
}

// sin for x already reduced to [-pi, pi] (parabola + one Newton-ish refine, P=0.225).
inline float FastSinReduced_(float x)
{
    const float B = 1.27323954473516f;  // 4/pi
    const float C = -0.405284734569351f; // -4/pi^2
    float       y = B * x + C * x * std::fabs(x);
    const float P = 0.225f;
    y             = P * (y * std::fabs(y) - y) + y; // extra precision
    return y;
}

// sin and cos of an arbitrary angle, computed together (the recompose path needs both).
inline void FastSinCos_(float a, float& s, float& c)
{
    const float kPi      = 3.14159265358979f;
    const float kTwoPi   = 6.28318530717959f;
    const float kHalfPi  = 1.57079632679490f;
    const float kInvTwoPi = 0.159154943091895f;
    // Wrap a into [-pi, pi].
    a -= kTwoPi * std::floor((a + kPi) * kInvTwoPi);
    s = FastSinReduced_(a);
    // cos(a) = sin(a + pi/2); keep the argument inside [-pi, pi].
    float ca = a + kHalfPi;
    if(ca > kPi) ca -= kTwoPi;
    c = FastSinReduced_(ca);
}

// Wrap an angle into [-pi, pi].
inline float WrapPhase_(float a)
{
    const float kPi      = 3.14159265358979f;
    const float kTwoPi   = 6.28318530717959f;
    const float kInvTwoPi = 0.159154943091895f;
    return a - kTwoPi * std::floor((a + kPi) * kInvTwoPi);
}

// Magnitude only (no atan2) — for effects that drive their own synthesis phase.
inline void MagOnly(const float* re, const float* im, float* mag, int nb)
{
    for(int k = 0; k <= nb; ++k)
        mag[k] = std::sqrt(re[k] * re[k] + im[k] * im[k]);
}

// Polar decompose: one-sided spectrum re/im[0..nb] -> magnitude/phase[0..nb].
inline void PolarFwd(const float* re, const float* im, float* mag, float* phase, int nb)
{
    for(int k = 0; k <= nb; ++k)
    {
        mag[k]   = std::sqrt(re[k] * re[k] + im[k] * im[k]);
        phase[k] = FastAtan2_(im[k], re[k]);
    }
}

// Polar recompose: magnitude/phase[0..nb] -> one-sided spectrum re/im[0..nb].
inline void PolarInv(const float* mag, const float* phase, float* re, float* im, int nb)
{
    for(int k = 0; k <= nb; ++k)
    {
        float s, c;
        FastSinCos_(phase[k], s, c);
        re[k] = mag[k] * c;
        im[k] = mag[k] * s;
    }
}

// --- Tools 3 & 4: spectral peak detection + fractional-bin splatter ---

// A detected spectral peak: sub-bin frequency (fractional bin index), magnitude, and
// the source bin's phase (so a frequency-shifted copy can be made phase-coherent).
struct SpectralPeak
{
    float bin;   // parabola-interpolated peak position (fractional bin)
    float mag;   // peak magnitude
    float phase; // source-bin phase
};

// Tool 3 — peak detector. Find local maxima of mag[0..nb] above a floor (the larger
// of floorFrac*globalMax and a tiny absolute), with 3-point parabolic interpolation
// for sub-bin frequency precision (needed to place sidebands at literal just-interval
// frequencies). Fills out[0..count-1]; returns count (<= maxPeaks). phase[] (optional,
// may be nullptr) supplies each peak's source phase; pass nullptr if unused.
inline int DetectPeaks(const float* mag, const float* phase, int nb,
                       SpectralPeak* out, int maxPeaks, float floorFrac)
{
    float gmax = 0.0f;
    for(int k = 0; k <= nb; ++k)
        if(mag[k] > gmax) gmax = mag[k];
    if(gmax < 1.0e-6f)
        return 0;
    float floor = floorFrac * gmax;
    if(floor < 1.0e-6f) floor = 1.0e-6f;

    int n = 0;
    for(int k = 1; k < nb && n < maxPeaks; ++k)
    {
        const float m = mag[k];
        if(m > floor && m >= mag[k - 1] && m > mag[k + 1])
        {
            const float a = mag[k - 1], c = mag[k + 1];
            const float denom = a - 2.0f * m + c;
            float       d     = (denom != 0.0f) ? 0.5f * (a - c) / denom : 0.0f;
            if(d > 0.5f) d = 0.5f;
            else if(d < -0.5f) d = -0.5f;
            out[n].bin   = static_cast<float>(k) + d;
            out[n].mag   = m;
            out[n].phase = phase ? phase[k] : 0.0f;
            ++n;
        }
    }
    return n;
}

// Tool 4 — fractional-bin splatter. ADD a complex partial (mag * e^{j*phase}) into the
// one-sided spectrum re/im[0..nb] at fractional position `bin`, linearly distributed
// across the two straddling bins. Used to place harmonic sidebands at non-integer
// (e.g. just-interval) target frequencies. No-op if out of range.
inline void SplatterAdd(float* re, float* im, float bin, float mag, float phase, int nb)
{
    if(bin < 0.0f || bin > static_cast<float>(nb))
        return;
    const int   t0   = static_cast<int>(bin);
    const float frac = bin - static_cast<float>(t0);
    float       s, c;
    FastSinCos_(phase, s, c);
    const float vr = mag * c, vi = mag * s;
    re[t0] += (1.0f - frac) * vr;
    im[t0] += (1.0f - frac) * vi;
    const int t1 = t0 + 1;
    if(t1 <= nb)
    {
        re[t1] += frac * vr;
        im[t1] += frac * vi;
    }
}

} // namespace craft
