#pragma once

#include <cmath>
#include <cstddef>

// Two-band tilt: peaking lows at f0/sqrt(2), peaking highs at f0*sqrt(2) => f_high = 2*f_low.
// Q is shared by both bells (RBJ peaking / bell), typically 0.5..1.7.

static constexpr float kTiltEqQMin       = 0.5f;
static constexpr float kTiltEqQMax       = 1.7f;
static constexpr float kTiltEqQDefault   = 1.2f;
static constexpr float kTiltEqTiltMaxDb    = 9.0f;
static constexpr float kTiltEqFreqMinHz    = 80.0f;
static constexpr float kTiltEqFreqMaxHz    = 10000.0f;

float TiltEq_CenterNormToHz(float center_norm_clamped01);
void  TiltEq_LowHighHzFromCenter(float center_hz, float& out_low_hz, float& out_high_hz);

struct TiltEqPeakingCoef
{
    float b0 = 1.f;
    float b1 = 0.f;
    float b2 = 0.f;
    float a1 = 0.f;
    float a2 = 0.f;
};

void TiltEq_SetPeakingCoef(TiltEqPeakingCoef& c, float fc_hz, float Q, float gain_db, float sample_rate);

float TiltEq_PeakingMagnitudeDb(const TiltEqPeakingCoef& c, float f_hz, float sample_rate);

float TiltEq_CascadeMagnitudeDb(float center_hz,
                                float tilt_db,
                                float f_hz,
                                float sample_rate,
                                float Q);

struct TiltEqBiquad
{
    TiltEqPeakingCoef coef{};
    float             z1 = 0.f;
    float             z2 = 0.f;

    void Reset()
    {
        z1 = z2 = 0.f;
    }

    void SetPeaking(float fc_hz, float Q, float gain_db, float sample_rate)
    {
        TiltEq_SetPeakingCoef(coef, fc_hz, Q, gain_db, sample_rate);
    }

    float Process(float x)
    {
        const float w = x - coef.a1 * z1 - coef.a2 * z2;
        const float y = coef.b0 * w + coef.b1 * z1 + coef.b2 * z2;
        z2            = z1;
        z1            = w;
        return y;
    }
};

struct TiltEqStereo
{
    TiltEqBiquad low_l_{};
    TiltEqBiquad low_r_{};
    TiltEqBiquad high_l_{};
    TiltEqBiquad high_r_{};

    void Reset();
    void SetFromParams(float center_hz, float tilt_db, float sample_rate, float Q);
    void ProcessSample(float& l, float& r, float dry_wet /*0..1*/);
    // Block variant: hoists biquad coefficients and z1/z2 state into stack
    // locals for the duration of the block, writes them back once at the end.
    // Produces the same sequence of operations as calling ProcessSample n
    // times, with fewer member-through-this loads/stores in the hot loop.
    void ProcessBlock(float* L, float* R, size_t n, float dry_wet /*0..1*/);
};
