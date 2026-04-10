#include "tilt_eq.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

float TiltEq_CenterNormToHz(float center_norm_clamped01)
{
    float n = center_norm_clamped01;
    if(n < 0.0f)
        n = 0.0f;
    else if(n > 1.0f)
        n = 1.0f;
    const float ln0 = std::log(kTiltEqFreqMinHz);
    const float ln1 = std::log(kTiltEqFreqMaxHz);
    return std::exp(ln0 + n * (ln1 - ln0));
}

void TiltEq_LowHighHzFromCenter(float center_hz, float& out_low_hz, float& out_high_hz)
{
    static constexpr float kSqrt2 = 1.4142135624f;
    out_low_hz  = center_hz / kSqrt2;
    out_high_hz = center_hz * kSqrt2;
}

static float TiltEq_ClampQ(float Q)
{
    if(Q < kTiltEqQMin)
        return kTiltEqQMin;
    if(Q > kTiltEqQMax)
        return kTiltEqQMax;
    return Q;
}

void TiltEq_SetPeakingCoef(TiltEqPeakingCoef& c, float fc_hz, float Q, float gain_db, float sample_rate)
{
    if(sample_rate <= 0.0f || Q <= 0.0f)
    {
        c.b0 = 1.f;
        c.b1 = c.b2 = 0.f;
        c.a1 = c.a2 = 0.f;
        return;
    }

    const float ny = 0.49f * sample_rate;
    float       fc = fc_hz;
    if(fc < 1.0f)
        fc = 1.0f;
    else if(fc > ny)
        fc = ny;

    const float w0 = 2.0f * static_cast<float>(M_PI) * (fc / sample_rate);
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * Q);
    // RBJ peaking EQ (audio-eq-cookbook): A = 10^(dB/40)
    const float A = std::pow(10.0f, gain_db * (1.0f / 40.0f));

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cosw0;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha / A;

    const float inv_a0 = 1.0f / a0;
    c.b0               = b0 * inv_a0;
    c.b1               = b1 * inv_a0;
    c.b2               = b2 * inv_a0;
    c.a1               = a1 * inv_a0;
    c.a2               = a2 * inv_a0;
}

static float BiquadMagnitudeDb(const TiltEqPeakingCoef& c, float w)
{
    const float cw  = std::cos(w);
    const float sw  = std::sin(w);
    const float c2w = std::cos(2.0f * w);
    const float s2w = std::sin(2.0f * w);

    const float nre = c.b0 + c.b1 * cw + c.b2 * c2w;
    const float nim = -c.b1 * sw - c.b2 * s2w;
    const float dre = 1.0f + c.a1 * cw + c.a2 * c2w;
    const float dim = -c.a1 * sw - c.a2 * s2w;

    const float num_mag = std::sqrt(nre * nre + nim * nim);
    const float den_mag = std::sqrt(dre * dre + dim * dim);
    if(den_mag <= 1e-20f || num_mag <= 1e-20f)
        return -200.0f;
    return 20.0f * std::log10(num_mag / den_mag);
}

float TiltEq_PeakingMagnitudeDb(const TiltEqPeakingCoef& c, float f_hz, float sample_rate)
{
    if(sample_rate <= 0.0f || f_hz <= 0.0f)
        return 0.0f;
    const float w = 2.0f * static_cast<float>(M_PI) * (f_hz / sample_rate);
    return BiquadMagnitudeDb(c, w);
}

float TiltEq_CascadeMagnitudeDb(float center_hz, float tilt_db, float f_hz, float sample_rate, float Q)
{
    const float         qq = TiltEq_ClampQ(Q);
    float               fl = 0.f;
    float               fh = 0.f;
    TiltEqPeakingCoef cl{};
    TiltEqPeakingCoef ch{};
    TiltEq_LowHighHzFromCenter(center_hz, fl, fh);
    TiltEq_SetPeakingCoef(cl, fl, qq, tilt_db, sample_rate);
    TiltEq_SetPeakingCoef(ch, fh, qq, -tilt_db, sample_rate);
    return TiltEq_PeakingMagnitudeDb(cl, f_hz, sample_rate)
           + TiltEq_PeakingMagnitudeDb(ch, f_hz, sample_rate);
}

void TiltEqStereo::Reset()
{
    low_l_.Reset();
    low_r_.Reset();
    high_l_.Reset();
    high_r_.Reset();
}

void TiltEqStereo::SetFromParams(float center_hz, float tilt_db, float sample_rate, float Q)
{
    const float qq = TiltEq_ClampQ(Q);
    float       fl = 0.f;
    float       fh = 0.f;
    TiltEq_LowHighHzFromCenter(center_hz, fl, fh);
    low_l_.SetPeaking(fl, qq, tilt_db, sample_rate);
    low_r_.SetPeaking(fl, qq, tilt_db, sample_rate);
    high_l_.SetPeaking(fh, qq, -tilt_db, sample_rate);
    high_r_.SetPeaking(fh, qq, -tilt_db, sample_rate);
}

void TiltEqStereo::ProcessSample(float& l, float& r, float dry_wet)
{
    float w = dry_wet;
    if(w < 0.0f)
        w = 0.0f;
    else if(w > 1.0f)
        w = 1.0f;

    const float inL = l;
    const float inR = r;
    const float eqL = high_l_.Process(low_l_.Process(inL));
    const float eqR = high_r_.Process(low_r_.Process(inR));
    l               = inL + w * (eqL - inL);
    r               = inR + w * (eqR - inR);
}
