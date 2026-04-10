#include "delay_biquad.h"
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

void DelayMidBiquad::SetLowpass(float fc_hz, float sample_rate, float Q)
{
    if(sample_rate <= 1.f || Q <= 1e-6f)
    {
        b0 = 1.f;
        b1 = b2 = a1 = a2 = 0.f;
        return;
    }
    const float nyq = 0.5f * sample_rate;
    float         fc  = fc_hz;
    if(fc < 1.f)
        fc = 1.f;
    if(fc > nyq * 0.49f)
        fc = nyq * 0.49f;

    const float w0 = 2.f * kPi * fc / sample_rate;
    const float c  = std::cos(w0);
    const float s  = std::sin(w0);
    const float a  = s / (2.f * Q);

    float b0_ = (1.f - c) * 0.5f;
    float b1_ = 1.f - c;
    float b2_ = (1.f - c) * 0.5f;
    float a0  = 1.f + a;
    float a1_ = -2.f * c;
    float a2_ = 1.f - a;

    b0 = b0_ / a0;
    b1 = b1_ / a0;
    b2 = b2_ / a0;
    a1 = a1_ / a0;
    a2 = a2_ / a0;
}

void DelayMidBiquad::SetHighpass(float fc_hz, float sample_rate, float Q)
{
    if(sample_rate <= 1.f || Q <= 1e-6f)
    {
        b0 = 1.f;
        b1 = b2 = a1 = a2 = 0.f;
        return;
    }
    const float nyq = 0.5f * sample_rate;
    float         fc  = fc_hz;
    if(fc < 1.f)
        fc = 1.f;
    if(fc > nyq * 0.49f)
        fc = nyq * 0.49f;

    const float w0 = 2.f * kPi * fc / sample_rate;
    const float c  = std::cos(w0);
    const float s  = std::sin(w0);
    const float a  = s / (2.f * Q);

    float b0_ = (1.f + c) * 0.5f;
    float b1_ = -(1.f + c);
    float b2_ = (1.f + c) * 0.5f;
    float a0  = 1.f + a;
    float a1_ = -2.f * c;
    float a2_ = 1.f - a;

    b0 = b0_ / a0;
    b1 = b1_ / a0;
    b2 = b2_ / a0;
    a1 = a1_ / a0;
    a2 = a2_ / a0;
}
