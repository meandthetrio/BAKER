#include "voice_engine_internal.h"

#include <cmath>

static constexpr float kTwoPi = 6.2831853071795864769f;
static constexpr float kEmphasisDriveUiMaxLinear = 1.995262315f;

static inline float FastTanh(float x)
{
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline float Clamp01(float value)
{
    if(value < 0.0f)
        return 0.0f;
    if(value > 1.0f)
        return 1.0f;
    return value;
}

void VoiceEngine::RecomputeLayerEmphasisCoeffs_(uint8_t layer)
{
    layer &= 1u;
    LayerEmphasisCoeffs& c = emphasis_coeff_[layer];

    float drive_linear = engine_gain_linear_[layer];
    if(drive_linear < 1.0f)
        drive_linear = 1.0f;
    const float drive_norm = (drive_linear - 1.0f) / (kEmphasisDriveUiMaxLinear - 1.0f);
    const float clamped_drive_norm = Clamp01(drive_norm);
    const float drive_taper = std::pow(clamped_drive_norm, 1.8f);
    const float shape_blend = std::pow(clamped_drive_norm, 0.72f);
    const float pre_gain = 1.0f + (27.0f * drive_taper);
    // Measured 2026-06-12: old `1/(1+0.24*drive_taper)` left a 20 dB post-emphasis
    // swing from drive=0 to drive=1 (sat pre_gain ~28x with almost no makeup).
    // This curve compensates the saturator gain so post-emphasis peak stays within
    // ~2 dB of drive=0 across the range.
    const float base_makeup = 1.0f / std::pow(1.0f + (4.0f * drive_taper), 1.5f);

    c.odd_drive = (engine_drive_mode_[layer] == 0u);
    c.odd_mix   = c.odd_drive ? 1.0f : 0.0f;
    c.pre_gain = pre_gain;
    c.shape_blend = shape_blend;
    c.base_makeup = base_makeup;
    c.positive_drive = pre_gain * (1.18f + (0.34f * drive_taper));
    c.negative_drive = pre_gain * (0.72f + (0.08f * drive_taper));
    c.even_makeup = base_makeup * (1.04f + (0.16f * drive_taper));

    float cutoff_hz = engine_filter_cutoff_hz_[layer];
    if(cutoff_hz < 20.0f)
        cutoff_hz = 20.0f;
    if(cutoff_hz > 20000.0f)
        cutoff_hz = 20000.0f;

    float g = 1.0f - std::exp((-kTwoPi * cutoff_hz) / sample_rate_);
    if(g < 0.0015f)
        g = 0.0015f;
    if(g > 0.70f)
        g = 0.70f;
    c.g = g;

    float resonance = engine_filter_resonance_[layer];
    if(resonance < 0.0f)
        resonance = 0.0f;
    if(resonance > 1.0f)
        resonance = 1.0f;
    const float resonance_shaped
        = resonance * resonance * (1.5f - (0.5f * resonance));
    c.feedback = resonance_shaped * (3.85f - (0.5f * g));
    c.pole4_linear_scale = 1.0f - (0.18f * resonance_shaped);
    c.tanh_input_scale = 1.12f + (0.32f * resonance_shaped);
}

// One-pole per-sample ramp coefficient. 1/2400 ≈ 50 ms at 48 kHz — matches the
// delay/sat smoothers in audio_engine.cpp. Kills the block-rate zipper when
// Express (or any source) sweeps cutoff/drive quickly.
static constexpr float kEmphasisCoeffSmoothCoeff = 1.0f / 2400.0f;

float VoiceEngine::ProcessLayerBusSample_(uint8_t layer, float input)
{
    layer &= 1u;
    LayerBusState& state = layer_bus_state_[layer];
    const LayerEmphasisCoeffs& t = emphasis_coeff_[layer];
    LayerEmphasisCoeffs& c = emphasis_coeff_z_[layer];

    c.odd_drive = t.odd_drive;

    // Ramp every used coefficient toward its target.
    c.odd_mix            += (t.odd_mix            - c.odd_mix)            * kEmphasisCoeffSmoothCoeff;
    c.pre_gain           += (t.pre_gain           - c.pre_gain)           * kEmphasisCoeffSmoothCoeff;
    c.shape_blend        += (t.shape_blend        - c.shape_blend)        * kEmphasisCoeffSmoothCoeff;
    c.base_makeup        += (t.base_makeup        - c.base_makeup)        * kEmphasisCoeffSmoothCoeff;
    c.positive_drive     += (t.positive_drive     - c.positive_drive)     * kEmphasisCoeffSmoothCoeff;
    c.negative_drive     += (t.negative_drive     - c.negative_drive)     * kEmphasisCoeffSmoothCoeff;
    c.even_makeup        += (t.even_makeup        - c.even_makeup)        * kEmphasisCoeffSmoothCoeff;
    c.g                  += (t.g                  - c.g)                  * kEmphasisCoeffSmoothCoeff;
    c.feedback           += (t.feedback           - c.feedback)           * kEmphasisCoeffSmoothCoeff;
    c.pole4_linear_scale += (t.pole4_linear_scale - c.pole4_linear_scale) * kEmphasisCoeffSmoothCoeff;
    c.tanh_input_scale   += (t.tanh_input_scale   - c.tanh_input_scale)   * kEmphasisCoeffSmoothCoeff;

    // Crossfade odd/even paths via odd_mix to avoid a click at mode switch.
    // Both branches are evaluated only during the brief (~50 ms) transition;
    // steady-state takes the cheap single-path branch.
    float driven;
    if(c.odd_mix > 0.999f)
    {
        const float odd_core = FastTanh(input * c.pre_gain);
        const float odd_shaped = input + ((odd_core - input) * c.shape_blend);
        driven = odd_shaped * c.base_makeup;
    }
    else if(c.odd_mix < 0.001f)
    {
        const float pos_core = (input > 0.0f) ? FastTanh(input * c.positive_drive) : 0.0f;
        const float neg_core = (input < 0.0f) ? -FastTanh((-input) * c.negative_drive) : 0.0f;
        const float asym_core = pos_core + neg_core;
        const float even_shaped = input + ((asym_core - input) * c.shape_blend);
        const float asym = even_shaped * c.even_makeup;
        const float dc_blocked = asym - state.drive_dc_x + (0.995f * state.drive_dc_y);
        state.drive_dc_x = asym;
        state.drive_dc_y = dc_blocked;
        driven = dc_blocked;
    }
    else
    {
        const float odd_core = FastTanh(input * c.pre_gain);
        const float odd_shaped = input + ((odd_core - input) * c.shape_blend);
        const float odd_driven = odd_shaped * c.base_makeup;

        const float pos_core = (input > 0.0f) ? FastTanh(input * c.positive_drive) : 0.0f;
        const float neg_core = (input < 0.0f) ? -FastTanh((-input) * c.negative_drive) : 0.0f;
        const float asym_core = pos_core + neg_core;
        const float even_shaped = input + ((asym_core - input) * c.shape_blend);
        const float asym = even_shaped * c.even_makeup;
        const float dc_blocked = asym - state.drive_dc_x + (0.995f * state.drive_dc_y);
        state.drive_dc_x = asym;
        state.drive_dc_y = dc_blocked;
        const float even_driven = dc_blocked;

        driven = (odd_driven * c.odd_mix) + (even_driven * (1.0f - c.odd_mix));
    }

    float ladder_in = driven - c.feedback * (state.pole4 - (0.12f * state.pole3));
    ladder_in = FastTanh(ladder_in);

    state.pole1 += c.g * (ladder_in - state.pole1);
    state.pole2 += c.g * (state.pole1 - state.pole2);
    state.pole3 += c.g * (state.pole2 - state.pole3);
    state.pole4 += c.g * (state.pole3 - state.pole4);

    float out = state.pole4 * c.pole4_linear_scale;
    out = FastTanh(out * c.tanh_input_scale);
    return out * 0.97f;
}
