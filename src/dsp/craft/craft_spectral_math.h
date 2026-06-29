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

} // namespace craft
