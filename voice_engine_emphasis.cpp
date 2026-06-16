#include "voice_engine_internal.h"

#include <cmath>

static constexpr float kTwoPi = 6.2831853071795864769f;
static constexpr float kEmphasisDriveUiMaxLinear = 1.995262315f;

// Resonance knob taper. The ladder's perceived resonance is already compressive
// near self-oscillation (peak barely moves at low feedback, then explodes as
// k->4). The old `res^2*(1.5-0.5*res)` curve was BACK-loaded, compounding that
// and leaving the bottom half of the knob inaudible. A front-loaded exponent
// (<1) ramps feedback quickly through the dead low range and eases near the top
// where the self-osc zone needs fine control. Tunable ~0.5 (more bottom) to
// ~0.75 (gentler bottom).
static constexpr float kResonanceCurveExp = 0.6f;

// Bass/passband gain compensation. Ladder feedback drops the passband by
// ~1/(1+k); these scale the forward signal by (1 + k*comp) to push it back up so
// resonance stops thinning the body. Full restore ~= 0.88; we use partial so the
// resonant peak (already loud) doesn't slam the feedback tanh too hard. HP gets
// none -- removing lows is the point there. Tunable by ear.
static constexpr float kBassCompLp = 0.5f;
static constexpr float kBassCompBp = 0.3f;

// Per-mode output trim, since the HP/BP tap mixes have different gain than the
// LP4 tap the resonance makeup was tuned against. Tunable by ear.
static constexpr float kFilterTrimLp = 1.0f;
static constexpr float kFilterTrimHp = 0.5f;
static constexpr float kFilterTrimBp = 0.7f;

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
    const float resonance_shaped = std::pow(resonance, kResonanceCurveExp);
    c.feedback = resonance_shaped * (3.85f - (0.5f * g));
    c.pole4_linear_scale = 1.0f - (0.18f * resonance_shaped);
    c.tanh_input_scale = 1.12f + (0.32f * resonance_shaped);

    // Multimode tap mix. Output = tap_in*x + tap1*y1 + tap2*y2 + tap3*y3 +
    // tap4*y4, with x = ladder input and y1..y4 the pole outputs:
    //   LP4 = y4
    //   HP4 = (1-LP)^4 = x - 4y1 + 6y2 - 4y3 + y4
    //   BP  = 4*(1-LP)^2*LP^2 = 4y2 - 8y3 + 4y4
    // Because the mix is linear in these coefficients, ramping them per-sample
    // morphs LP<->HP<->BP without a click. input_comp restores passband level.
    const uint8_t mode = engine_filter_mode_[layer] & 3u;
    if(mode == 1u) // HP
    {
        c.tap_in = 1.0f;  c.tap1 = -4.0f; c.tap2 = 6.0f;  c.tap3 = -4.0f; c.tap4 = 1.0f;
        c.input_comp = 1.0f;
        c.output_trim = kFilterTrimHp;
    }
    else if(mode == 2u) // BP
    {
        c.tap_in = 0.0f;  c.tap1 = 0.0f;  c.tap2 = 4.0f;  c.tap3 = -8.0f; c.tap4 = 4.0f;
        c.input_comp = 1.0f + (kBassCompBp * c.feedback);
        c.output_trim = kFilterTrimBp;
    }
    else // LP
    {
        c.tap_in = 0.0f;  c.tap1 = 0.0f;  c.tap2 = 0.0f;  c.tap3 = 0.0f;  c.tap4 = 1.0f;
        c.input_comp = 1.0f + (kBassCompLp * c.feedback);
        c.output_trim = kFilterTrimLp;
    }
}

// One-pole per-sample ramp coefficient. 1/2400 ≈ 50 ms at 48 kHz — matches the
// delay/sat smoothers in audio_engine.cpp. Kills the block-rate zipper when
// Express (or any source) sweeps cutoff/drive quickly.
static constexpr float kEmphasisCoeffSmoothCoeff = 1.0f / 2400.0f;

// One sample of the emphasis chain: ladder filter (cutoff/resonance) FIRST,
// then drive (saturation) on the filtered signal. Factored out so the batched
// block processor below single-sources the math while still inlining at -O2
// (same TU). `s` carries the filter/DC state across samples; `c` is the
// effective coefficient set for this sample (fixed in steady state, ramped
// during a transition). Bit-identical to the previous per-sample path.
static inline float EmphasisRunSample_(LayerBusState& s, const LayerEmphasisCoeffs& c, float input)
{
    // ---- Phase 1: ladder filter (cutoff / resonance) on the raw input ----
    float ladder_in = (input * c.input_comp) - c.feedback * (s.pole4 - (0.12f * s.pole3));
    ladder_in = FastTanh(ladder_in);

    s.pole1 += c.g * (ladder_in - s.pole1);
    s.pole2 += c.g * (s.pole1 - s.pole2);
    s.pole3 += c.g * (s.pole2 - s.pole3);
    s.pole4 += c.g * (s.pole3 - s.pole4);

    // Multimode tap mix (x = ladder_in). Steady-state this is one mode's fixed
    // coefficient vector; during a mode change the ramped coeffs morph it.
    const float tapped = (c.tap_in * ladder_in) + (c.tap1 * s.pole1)
                         + (c.tap2 * s.pole2) + (c.tap3 * s.pole3)
                         + (c.tap4 * s.pole4);

    float filtered = tapped * c.pole4_linear_scale * c.output_trim;
    filtered = FastTanh(filtered * c.tanh_input_scale);
    filtered *= 0.97f;

    // ---- Phase 2: drive (saturation) on the filtered signal ----
    // Crossfade odd/even paths via odd_mix to avoid a click at mode switch.
    // Both branches are evaluated only during the brief (~50 ms) transition;
    // steady-state takes the cheap single-path branch.
    const float x = filtered;
    float driven;
    if(c.odd_mix > 0.999f)
    {
        const float odd_core = FastTanh(x * c.pre_gain);
        const float odd_shaped = x + ((odd_core - x) * c.shape_blend);
        driven = odd_shaped * c.base_makeup;
    }
    else if(c.odd_mix < 0.001f)
    {
        const float pos_core = (x > 0.0f) ? FastTanh(x * c.positive_drive) : 0.0f;
        const float neg_core = (x < 0.0f) ? -FastTanh((-x) * c.negative_drive) : 0.0f;
        const float asym_core = pos_core + neg_core;
        const float even_shaped = x + ((asym_core - x) * c.shape_blend);
        const float asym = even_shaped * c.even_makeup;
        const float dc_blocked = asym - s.drive_dc_x + (0.995f * s.drive_dc_y);
        s.drive_dc_x = asym;
        s.drive_dc_y = dc_blocked;
        driven = dc_blocked;
    }
    else
    {
        const float odd_core = FastTanh(x * c.pre_gain);
        const float odd_shaped = x + ((odd_core - x) * c.shape_blend);
        const float odd_driven = odd_shaped * c.base_makeup;

        const float pos_core = (x > 0.0f) ? FastTanh(x * c.positive_drive) : 0.0f;
        const float neg_core = (x < 0.0f) ? -FastTanh((-x) * c.negative_drive) : 0.0f;
        const float asym_core = pos_core + neg_core;
        const float even_shaped = x + ((asym_core - x) * c.shape_blend);
        const float asym = even_shaped * c.even_makeup;
        const float dc_blocked = asym - s.drive_dc_x + (0.995f * s.drive_dc_y);
        s.drive_dc_x = asym;
        s.drive_dc_y = dc_blocked;
        const float even_driven = dc_blocked;

        driven = (odd_driven * c.odd_mix) + (even_driven * (1.0f - c.odd_mix));
    }

    return driven;
}

// One coefficient is "settled" when ramping it would change it by less than a
// relative epsilon. The (|t|+1) floor makes near-zero coeffs (e.g. unused taps)
// use ~absolute eps while large coeffs (pre_gain ~28) use relative eps; the
// chosen 1e-4 keeps any settle-time snap below ~0.01 % — inaudible.
static inline bool EmphCoeffClose_(float t, float c)
{
    float d = t - c;
    if(d < 0.0f)
        d = -d;
    float a = t;
    if(a < 0.0f)
        a = -a;
    return d <= 1.0e-4f * (a + 1.0f);
}

static inline bool EmphasisCoeffsSettled_(const LayerEmphasisCoeffs& t, const LayerEmphasisCoeffs& c)
{
    return EmphCoeffClose_(t.odd_mix, c.odd_mix) && EmphCoeffClose_(t.pre_gain, c.pre_gain)
           && EmphCoeffClose_(t.shape_blend, c.shape_blend)
           && EmphCoeffClose_(t.base_makeup, c.base_makeup)
           && EmphCoeffClose_(t.positive_drive, c.positive_drive)
           && EmphCoeffClose_(t.negative_drive, c.negative_drive)
           && EmphCoeffClose_(t.even_makeup, c.even_makeup) && EmphCoeffClose_(t.g, c.g)
           && EmphCoeffClose_(t.feedback, c.feedback)
           && EmphCoeffClose_(t.pole4_linear_scale, c.pole4_linear_scale)
           && EmphCoeffClose_(t.tanh_input_scale, c.tanh_input_scale)
           && EmphCoeffClose_(t.tap_in, c.tap_in) && EmphCoeffClose_(t.tap1, c.tap1)
           && EmphCoeffClose_(t.tap2, c.tap2) && EmphCoeffClose_(t.tap3, c.tap3)
           && EmphCoeffClose_(t.tap4, c.tap4) && EmphCoeffClose_(t.input_comp, c.input_comp);
}

// Batched per-layer emphasis. Replaces the old per-sample ProcessLayerBusSample_
// (a real cross-TU call 48x/block/layer at -O2, with the 18 coefficient ramps
// re-run every sample). Two wins, both bit-identical to the per-sample path:
//   1. Filter/DC state is hoisted into a local so it stays register-resident
//      across the block instead of being reloaded through `this` each sample.
//   2. The 18 coefficient ramps run only while the smoothed set is still
//      converging to target (a UI/mod change in the last ~50 ms). Once settled
//      we snap and read fixed coefficients — no per-sample ramp arithmetic.
void VoiceEngine::ProcessLayerBusBlock_(uint8_t layer, float* buf, size_t n, float out_scale)
{
    if(buf == nullptr || n == 0u)
        return;

    layer &= 1u;
    LayerBusState& state = layer_bus_state_[layer];
    const LayerEmphasisCoeffs& t = emphasis_coeff_[layer];
    LayerEmphasisCoeffs& cz = emphasis_coeff_z_[layer];

    cz.odd_drive = t.odd_drive;

    LayerBusState s = state; // hoist filter/DC state into a register-resident local

    if(EmphasisCoeffsSettled_(t, cz))
    {
        cz = t; // one-time sub-epsilon snap on settle; exact continuity thereafter
        const LayerEmphasisCoeffs c = cz;
        for(size_t i = 0; i < n; ++i)
            buf[i] = EmphasisRunSample_(s, c, buf[i]) * out_scale;
    }
    else
    {
        // Transition: ramp every used coefficient toward its target per sample
        // (de-zipper), then process — matches the old per-sample order exactly.
        for(size_t i = 0; i < n; ++i)
        {
            cz.odd_mix       += (t.odd_mix       - cz.odd_mix)       * kEmphasisCoeffSmoothCoeff;
            cz.pre_gain      += (t.pre_gain      - cz.pre_gain)      * kEmphasisCoeffSmoothCoeff;
            cz.shape_blend   += (t.shape_blend   - cz.shape_blend)   * kEmphasisCoeffSmoothCoeff;
            cz.base_makeup   += (t.base_makeup   - cz.base_makeup)   * kEmphasisCoeffSmoothCoeff;
            cz.positive_drive+= (t.positive_drive- cz.positive_drive)* kEmphasisCoeffSmoothCoeff;
            cz.negative_drive+= (t.negative_drive- cz.negative_drive)* kEmphasisCoeffSmoothCoeff;
            cz.even_makeup   += (t.even_makeup   - cz.even_makeup)   * kEmphasisCoeffSmoothCoeff;
            cz.g             += (t.g             - cz.g)             * kEmphasisCoeffSmoothCoeff;
            cz.feedback      += (t.feedback      - cz.feedback)      * kEmphasisCoeffSmoothCoeff;
            cz.pole4_linear_scale += (t.pole4_linear_scale - cz.pole4_linear_scale) * kEmphasisCoeffSmoothCoeff;
            cz.tanh_input_scale   += (t.tanh_input_scale   - cz.tanh_input_scale)   * kEmphasisCoeffSmoothCoeff;
            cz.tap_in        += (t.tap_in        - cz.tap_in)        * kEmphasisCoeffSmoothCoeff;
            cz.tap1          += (t.tap1          - cz.tap1)          * kEmphasisCoeffSmoothCoeff;
            cz.tap2          += (t.tap2          - cz.tap2)          * kEmphasisCoeffSmoothCoeff;
            cz.tap3          += (t.tap3          - cz.tap3)          * kEmphasisCoeffSmoothCoeff;
            cz.tap4          += (t.tap4          - cz.tap4)          * kEmphasisCoeffSmoothCoeff;
            cz.input_comp    += (t.input_comp    - cz.input_comp)    * kEmphasisCoeffSmoothCoeff;
            cz.output_trim   += (t.output_trim   - cz.output_trim)   * kEmphasisCoeffSmoothCoeff;

            buf[i] = EmphasisRunSample_(s, cz, buf[i]) * out_scale;
        }
    }

    state = s; // commit filter/DC state
}
