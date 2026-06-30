#pragma once

#include <cmath>
#include <cstddef>

// Two-band tilt: peaking lows at f0/sqrt(2), peaking highs at f0*sqrt(2) => f_high = 2*f_low.
// Q is shared by both bells (RBJ peaking / bell), typically 0.5..1.7.

static constexpr float kTiltEqQMin       = 0.4f;  // lower = wider bells
static constexpr float kTiltEqQMax       = 0.8f;  // skinniest the see-saw bells get
static constexpr float kTiltEqQDefault   = 0.6f;
// Bell mode (single peaking band) gets a much wider Q range so it can tighten to
// a narrow, surgical bell. Shares the eq_q value; clamped to this only in bell mode.
static constexpr float kEqBellQMax       = 8.0f;
static constexpr float kTiltEqTiltMaxDb    = 12.0f; // +/- per bell (was 9)
static constexpr float kTiltEqFreqMinHz    = 80.0f;
static constexpr float kTiltEqFreqMaxHz    = 10000.0f;

// Independent low/high shelf bands added alongside the tilt (Process->EQ). Each
// can be flipped to a filter (low band -> high-pass, high band -> low-pass),
// where the gain control becomes resonance (Q).
static constexpr float kEqShelfGainMaxDb = 12.0f;  // +/- shelf gain
static constexpr float kEqShelfNeutralDb = 0.05f;  // |gain| below this -> identity
static constexpr float kEqLoCutMinHz     = 20.0f;
static constexpr float kEqLoCutMaxHz     = 500.0f;
static constexpr float kEqHiCutMinHz     = 2000.0f;
static constexpr float kEqHiCutMaxHz     = 20000.0f;
static constexpr float kEqFilterQMin     = 0.5f;
static constexpr float kEqFilterQMax     = 4.0f;
static constexpr float kEqFilterQDefault = 0.707f;

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
void TiltEq_SetLowShelfCoef(TiltEqPeakingCoef& c, float fc_hz, float gain_db, float sample_rate);
void TiltEq_SetHighShelfCoef(TiltEqPeakingCoef& c, float fc_hz, float gain_db, float sample_rate);
// Slope-variable shelves (S in (0,1]; S=1 = steepest). Used by the tilt so its Q
// control sets the shelf slope while the response still reaches +/-gain at the
// extremes. The fixed-S=1 setters above just delegate here.
void TiltEq_SetLowShelfSlopeCoef(TiltEqPeakingCoef& c, float fc_hz, float gain_db, float S, float sample_rate);
void TiltEq_SetHighShelfSlopeCoef(TiltEqPeakingCoef& c, float fc_hz, float gain_db, float S, float sample_rate);
void TiltEq_SetHighpassCoef(TiltEqPeakingCoef& c, float fc_hz, float Q, float sample_rate);
void TiltEq_SetLowpassCoef(TiltEqPeakingCoef& c, float fc_hz, float Q, float sample_rate);

float TiltEq_PeakingMagnitudeDb(const TiltEqPeakingCoef& c, float f_hz, float sample_rate);

float TiltEq_CascadeMagnitudeDb(float center_hz,
                                float tilt_db,
                                float f_hz,
                                float sample_rate,
                                float Q,
                                bool bell_mode);

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

    void SetIdentity()
    {
        coef.b0 = 1.f;
        coef.b1 = coef.b2 = 0.f;
        coef.a1 = coef.a2 = 0.f;
    }
    void SetLowShelf(float fc_hz, float gain_db, float sample_rate)
    {
        TiltEq_SetLowShelfCoef(coef, fc_hz, gain_db, sample_rate);
    }
    void SetHighShelf(float fc_hz, float gain_db, float sample_rate)
    {
        TiltEq_SetHighShelfCoef(coef, fc_hz, gain_db, sample_rate);
    }
    void SetHighpass(float fc_hz, float Q, float sample_rate)
    {
        TiltEq_SetHighpassCoef(coef, fc_hz, Q, sample_rate);
    }
    void SetLowpass(float fc_hz, float Q, float sample_rate)
    {
        TiltEq_SetLowpassCoef(coef, fc_hz, Q, sample_rate);
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
    // Independent low-shelf/HP and high-shelf/LP bands (cascaded after the tilt).
    // Identity (b0=1) when the band is neutral, so they can always run cheaply.
    TiltEqBiquad lo_l_{};
    TiltEqBiquad lo_r_{};
    TiltEqBiquad hi_l_{};
    TiltEqBiquad hi_r_{};

    void Reset();
    // bell_mode false = tilt see-saw (low +tilt / high -tilt bells); true = a
    // single peaking bell at center_hz (high bell set to identity).
    void SetFromParams(float center_hz, float tilt_db, float sample_rate, float Q, bool bell_mode);
    // Low band: shelf at gain_db, or a high-pass (is_filter) where q is resonance.
    void SetLoBand(float cutoff_hz, float gain_db, bool is_filter, float q, float sample_rate);
    // High band: shelf at gain_db, or a low-pass (is_filter) where q is resonance.
    void SetHiBand(float cutoff_hz, float gain_db, bool is_filter, float q, float sample_rate);
    void ProcessSample(float& l, float& r, float dry_wet /*0..1*/);
    // Block variant: hoists biquad coefficients and z1/z2 state into stack
    // locals for the duration of the block, writes them back once at the end.
    // Produces the same sequence of operations as calling ProcessSample n
    // times, with fewer member-through-this loads/stores in the hot loop.
    void ProcessBlock(float* L, float* R, size_t n, float dry_wet /*0..1*/);
};
