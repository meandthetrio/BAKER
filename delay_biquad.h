#pragma once

// 2nd-order RBJ biquad (Butterworth via Q = 1/sqrt(2)); used for delay MID band-limiting.

struct DelayMidBiquad
{
    float b0 = 1.f;
    float b1 = 0.f;
    float b2 = 0.f;
    float a1 = 0.f;
    float a2 = 0.f;
    float z1 = 0.f;
    float z2 = 0.f;

    void Reset()
    {
        z1 = z2 = 0.f;
    }

    void SetLowpass(float fc_hz, float sample_rate, float Q);
    void SetHighpass(float fc_hz, float sample_rate, float Q);

    float Process(float x)
    {
        const float w = x - a1 * z1 - a2 * z2;
        const float y = b0 * w + b1 * z1 + b2 * z2;
        z2     = z1;
        z1     = w;
        return y;
    }
};
