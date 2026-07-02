#include "DattorroReverb.h"
#include "mem_regions.h"

#include <cmath>
#include <cstring>

namespace
{
// Sizes must match `DattorroReverb.h` input + tank allpass buffer constants.
static constexpr size_t kDtcInputAp1Max = 230;
static constexpr size_t kDtcInputAp2Max = 172;
static constexpr size_t kDtcInputAp3Max = 609;
static constexpr size_t kDtcInputAp4Max = 446;
static constexpr size_t kDtcTankAp1Max  = 960;
static constexpr size_t kDtcTankAp2Max  = 2880;
static constexpr size_t kDtcTankAp3Max  = 1440;
static constexpr size_t kDtcTankAp4Max  = 4272;

ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 float g_dattorro_input_ap1_buf[kDtcInputAp1Max];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 float g_dattorro_input_ap2_buf[kDtcInputAp2Max];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 float g_dattorro_input_ap3_buf[kDtcInputAp3Max];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 float g_dattorro_input_ap4_buf[kDtcInputAp4Max];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 float g_dattorro_input_ap1_r_buf[kDtcInputAp1Max];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 float g_dattorro_input_ap2_r_buf[kDtcInputAp2Max];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 float g_dattorro_input_ap3_r_buf[kDtcInputAp3Max];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 float g_dattorro_input_ap4_r_buf[kDtcInputAp4Max];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 float g_dattorro_tank_ap1_buf[kDtcTankAp1Max];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 float g_dattorro_tank_ap2_buf[kDtcTankAp2Max];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 float g_dattorro_tank_ap3_buf[kDtcTankAp3Max];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 float g_dattorro_tank_ap4_buf[kDtcTankAp4Max];

// Reverb tone mapping (used by Process/ProcessBlock control-rate updates).
//
// Darkness is split across two stages so it decouples from decay time:
//
//   1. In-loop damping filter (inside the tank feedback path). This is the
//      classic Dattorro "tail softens as it decays" character. Because it sits
//      in the feedback loop, its cutoff is ALSO frequency-dependent decay — the
//      lower it goes, the shorter the tail, no matter what `decay` is set to.
//      So we keep its range GENTLE (never below kDampCutoffDark) so it adds
//      realism without collapsing the tail.
//   2. Post-tank output lowpass (outside the loop, on the wet output). This
//      carries the heavy darkening. It is NOT in the feedback path, so it
//      darkens the wet with ZERO effect on tail length — this is what lets the
//      reverb be long AND dark at the same time.
//
// The input bandwidth filter is fixed and bright (classic Dattorro) — it is NOT
// driven by the damping knob. All damp-driven cutoffs are mapped exponentially
// so knob travel feels perceptually even.
// Decay fader -> tank feedback coefficient. The fader is normalized 0..1 and
// maps linearly onto [kDecayFbMin, kDecayFbMax]. The floor is well above zero
// so even the shortest setting still rings as a small room rather than a few
// discrete echoes (a near-zero feedback tank just sounds like a strange delay).
// The ceiling is high for a long tail but stays below ~0.97 so the loop never
// runs away / turns metallic.
constexpr float kDecayFbMin = 0.55f; // fader 0: short but real small-room tail
constexpr float kDecayFbMax = 0.94f; // fader 1: long tail

constexpr float kInputBandwidthHz = 16000.0f; // fixed bright input LPF
constexpr float kDampCutoffBright  = 11000.0f; // in-loop damp, damp = 0 (brightest)
constexpr float kDampCutoffDark    = 2500.0f;  // in-loop damp, damp = 1 (gentle floor)
constexpr float kOutLpfBright      = 18000.0f; // post-tank LPF, damp = 0 (effectively open)
constexpr float kOutLpfDark        = 1200.0f;  // post-tank LPF, damp = 1 (genuinely dark wet)

// True stereo chorus on the wet tail (driven by the `mod` knob). After the
// tank, each wet channel is run through its own short modulated delay line and
// blended back with the straight wet. The delay time is swept by two slow,
// incommensurate LFOs mixed differently per channel — the per-channel
// decorrelation produces the wide, swirly stereo image, and the non-harmonic
// rate ratio keeps the motion from sounding like a fixed periodic wobble. The
// sweep is centred at kChorusCenterSamples so the delay stays positive; reads
// are fractional (DelayLine interpolates) so the sweep is click-free.
//
// `mod` scales the wet/dry blend of the chorused voice from 0 (bypass — pure
// tank wet, no chorus) up to kChorusMaxMix. Sweep depth is fixed and musical;
// the knob controls how much chorus is folded in.
constexpr float kChorusRateAHz       = 0.60f;          // primary LFO
constexpr float kChorusRateBHz       = 0.91f;          // secondary LFO (incommensurate)
// Samples per ms at the reverb engine rate (halved when REVERB_HALF_RATE so the
// chorus timing stays the same in ms; every other length derives from
// sample_rate_ and scales automatically).
#if REVERB_HALF_RATE
constexpr float kRevSamplesPerMs     = 24.0f;
#else
constexpr float kRevSamplesPerMs     = 48.0f;
#endif
constexpr float kChorusCenterSamples = 11.0f * kRevSamplesPerMs; // ~11 ms center delay
constexpr float kChorusDepthSamples  = 5.0f * kRevSamplesPerMs;  // ~±5 ms sweep
constexpr float kChorusMaxMix        = 0.5f;           // wet/dry of chorus voice at mod = 1
// L<->R cross-feed of the chorus voices: 0 = straight (each side hears only its
// own delay line), 1 = full swap (each side hears the OPPOSITE line's wandering
// voice). Cross-coupling the detuned voices across channels is what widens the
// image into an ensemble. Tune to taste; 1 is the widest.
constexpr float kChorusCross         = 1.0f;

// Tail input-skip thresholds. The reverb input goes silent during a tail, so
// the whole input section (bandwidth filters, predelay, input allpasses, early
// reflections) just grinds through zeros. Once the input AND that section's
// output fall below these (~ -120 dBFS, inaudible — and the delay-line portion
// reaches exactly zero anyway), the section is bypassed for the rest of the
// tail. Self-calibrating: we wait for the section to actually empty, so the
// bypass and the later resume are click-free.
constexpr float kInputSilenceEps     = 1.0e-6f;
constexpr float kInputFlushEps       = 1.0e-6f;
} // namespace

// One-pole lowpass coefficient for a given cutoff: g = 1 - e^(-2*pi*fc/fs),
// clamped to [0,1]. Cheap to apply (y += g*(x-y)); recomputed only at the
// control rate, so the exp() cost is negligible.
static float OnePoleG_(float cutoff_hz, float sample_rate)
{
    float g = 1.0f - std::exp(-6.2831853072f * cutoff_hz / sample_rate);
    if(g < 0.0f) g = 0.0f;
    if(g > 1.0f) g = 1.0f;
    return g;
}

float DattorroReverb::Clamp01_(float value)
{
    if(value < 0.0f)
        return 0.0f;
    if(value > 1.0f)
        return 1.0f;
    return value;
}

void DattorroReverb::Allpass::Init(float* b, size_t max_length)
{
    buf      = b;
    max_len  = max_length;
    len      = max_len > 0 ? max_len - 1u : 0u;
    idx      = 0;
    feedback = 0.5f;
    Clear();
}

void DattorroReverb::Allpass::Clear()
{
    if(buf && max_len)
        std::memset(buf, 0, max_len * sizeof(float));
    idx = 0;
}

void DattorroReverb::Allpass::SetLength(size_t length)
{
    if(length > max_len)
        length = max_len;
    len = length;
    if(idx >= len && len > 0)
        idx = 0;
}

void DattorroReverb::Allpass::SetFeedback(float value)
{
    feedback = value;
}

ADSR2_ITCM_TEXT float DattorroReverb::Allpass::Process(float input)
{
    const float bufout = buf[idx];
    const float temp   = input * -feedback;
    const float output = bufout + temp;
    buf[idx] = input + ((bufout + temp) * feedback);
    if(++idx >= len)
        idx = 0;
    return output;
}

void DattorroReverb::StaticAllpassFourTap::Init(float* b, size_t max_length)
{
    buf      = b;
    max_len  = max_length;
    len      = max_len > 0 ? max_len - 1u : 0u;
    idx1 = idx2 = idx3 = idx4 = 0;
    feedback = 0.5f;
    Clear();
}

void DattorroReverb::StaticAllpassFourTap::Clear()
{
    if(buf && max_len)
        std::memset(buf, 0, max_len * sizeof(float));
    idx1 = idx2 = idx3 = idx4 = 0;
}

void DattorroReverb::StaticAllpassFourTap::SetLength(size_t length)
{
    if(length > max_len)
        length = max_len;
    len = length;
}

void DattorroReverb::StaticAllpassFourTap::SetFeedback(float value)
{
    feedback = value;
}

void DattorroReverb::StaticAllpassFourTap::SetIndex(size_t i1,
                                                    size_t i2,
                                                    size_t i3,
                                                    size_t i4)
{
    idx1 = len > 0 ? (i1 % len) : 0;
    idx2 = len > 0 ? (i2 % len) : 0;
    idx3 = len > 0 ? (i3 % len) : 0;
    idx4 = len > 0 ? (i4 % len) : 0;
}

ADSR2_ITCM_TEXT float DattorroReverb::StaticAllpassFourTap::Process(float input)
{
    const float bufout = buf[idx1];
    const float temp   = input * -feedback;
    const float output = bufout + temp;
    buf[idx1] = input + ((bufout + temp) * feedback);
    if(++idx1 >= len)
        idx1 = 0;
    if(++idx2 >= len)
        idx2 = 0;
    if(++idx3 >= len)
        idx3 = 0;
    if(++idx4 >= len)
        idx4 = 0;
    return output;
}

float DattorroReverb::StaticAllpassFourTap::GetIndex(size_t index) const
{
    switch(index)
    {
        case 0: return buf[idx1];
        case 1: return buf[idx2];
        case 2: return buf[idx3];
        case 3: return buf[idx4];
        default: return buf[idx1];
    }
}

void DattorroReverb::StaticDelayLineFourTap::Init(float* b, size_t max_length)
{
    buf     = b;
    max_len = max_length;
    len     = max_len > 0 ? max_len - 1u : 0u;
    idx1 = idx2 = idx3 = idx4 = 0;
    Clear();
}

void DattorroReverb::StaticDelayLineFourTap::Clear()
{
    if(buf && max_len)
        std::memset(buf, 0, max_len * sizeof(float));
    idx1 = idx2 = idx3 = idx4 = 0;
}

void DattorroReverb::StaticDelayLineFourTap::SetLength(size_t length)
{
    if(length > max_len)
        length = max_len;
    len = length;
}

void DattorroReverb::StaticDelayLineFourTap::SetIndex(size_t i1,
                                                      size_t i2,
                                                      size_t i3,
                                                      size_t i4)
{
    idx1 = len > 0 ? (i1 % len) : 0;
    idx2 = len > 0 ? (i2 % len) : 0;
    idx3 = len > 0 ? (i3 % len) : 0;
    idx4 = len > 0 ? (i4 % len) : 0;
}

ADSR2_ITCM_TEXT float DattorroReverb::StaticDelayLineFourTap::Process(float input)
{
    const float output = buf[idx1];
    buf[idx1++] = input;
    if(idx1 >= len)
        idx1 = 0;
    if(++idx2 >= len)
        idx2 = 0;
    if(++idx3 >= len)
        idx3 = 0;
    if(++idx4 >= len)
        idx4 = 0;
    return output;
}

float DattorroReverb::StaticDelayLineFourTap::GetIndex(size_t index) const
{
    switch(index)
    {
        case 0: return buf[idx1];
        case 1: return buf[idx2];
        case 2: return buf[idx3];
        case 3: return buf[idx4];
        default: return buf[idx1];
    }
}

void DattorroReverb::StaticDelayLineEightTap::Init(float* b, size_t max_length)
{
    buf     = b;
    max_len = max_length;
    len     = max_len > 0 ? max_len - 1u : 0u;
    idx1 = idx2 = idx3 = idx4 = idx5 = idx6 = idx7 = idx8 = 0;
    Clear();
}

void DattorroReverb::StaticDelayLineEightTap::Clear()
{
    if(buf && max_len)
        std::memset(buf, 0, max_len * sizeof(float));
    idx1 = idx2 = idx3 = idx4 = idx5 = idx6 = idx7 = idx8 = 0;
}

void DattorroReverb::StaticDelayLineEightTap::SetLength(size_t length)
{
    if(length > max_len)
        length = max_len;
    len = length;
}

void DattorroReverb::StaticDelayLineEightTap::SetIndex(size_t i1,
                                                       size_t i2,
                                                       size_t i3,
                                                       size_t i4,
                                                       size_t i5,
                                                       size_t i6,
                                                       size_t i7,
                                                       size_t i8)
{
    idx1 = len > 0 ? (i1 % len) : 0;
    idx2 = len > 0 ? (i2 % len) : 0;
    idx3 = len > 0 ? (i3 % len) : 0;
    idx4 = len > 0 ? (i4 % len) : 0;
    idx5 = len > 0 ? (i5 % len) : 0;
    idx6 = len > 0 ? (i6 % len) : 0;
    idx7 = len > 0 ? (i7 % len) : 0;
    idx8 = len > 0 ? (i8 % len) : 0;
}

ADSR2_ITCM_TEXT float DattorroReverb::StaticDelayLineEightTap::Process(float input)
{
    const float output = buf[idx1];
    buf[idx1++] = input;
    if(idx1 >= len)
        idx1 = 0;
    if(++idx2 >= len)
        idx2 = 0;
    if(++idx3 >= len)
        idx3 = 0;
    if(++idx4 >= len)
        idx4 = 0;
    if(++idx5 >= len)
        idx5 = 0;
    if(++idx6 >= len)
        idx6 = 0;
    if(++idx7 >= len)
        idx7 = 0;
    if(++idx8 >= len)
        idx8 = 0;
    return output;
}

float DattorroReverb::StaticDelayLineEightTap::GetIndex(size_t index) const
{
    switch(index)
    {
        case 0: return buf[idx1];
        case 1: return buf[idx2];
        case 2: return buf[idx3];
        case 3: return buf[idx4];
        case 4: return buf[idx5];
        case 5: return buf[idx6];
        case 6: return buf[idx7];
        case 7: return buf[idx8];
        default: return buf[idx1];
    }
}

void DattorroReverb::StateVariable::Reset()
{
    low = high = band = notch = 0.0f;
}

void DattorroReverb::StateVariable::SetSampleRate(float value)
{
    // 2x internal oversampling. The loop count in Process() must match this factor
    // so that UpdateCoefficient()'s f = 2*sin(pi*freq/sample_rate) maps correctly.
    sample_rate = value * 2.0f;
    UpdateCoefficient();
}

void DattorroReverb::StateVariable::Frequency(float value)
{
    if(value == frequency)
        return;
    frequency = value;
    UpdateCoefficient();
}

void DattorroReverb::StateVariable::Resonance(float value)
{
    q = 2.0f - 2.0f * value;
}

void DattorroReverb::StateVariable::Type(FilterType value)
{
    type = value;
}

ADSR2_ITCM_TEXT float DattorroReverb::StateVariable::Process(float input)
{
    // Internal oversampling factor (paired with SetSampleRate multiplier).
    for(int i = 0; i < 2; ++i)
    {
        low += static_cast<float>(f * band + 1e-25f);
        high = input - low - q * band;
        band += f * high;
        notch = low + high;
    }

    switch(type)
    {
        case LOWPASS: return low;
        case HIGHPASS: return high;
        case BANDPASS: return band;
        case NOTCH: return notch;
        default: return low;
    }
}

void DattorroReverb::StateVariable::UpdateCoefficient()
{
    f = static_cast<float>(2.0f * std::sin(3.141592654f * frequency / sample_rate));
    // Stability clamp. For this Chamberlin SVF the discrete state matrix keeps
    // both poles inside the unit circle only while f < -q + sqrt(q*q + 4). At
    // the resonance-0 setting used here (q = 2) that limit is ~0.828. The
    // damping/bandwidth filters are driven up to ~18.5 kHz against the
    // 2x-oversampled 96 kHz rate, which at low damp pushes f past the limit and
    // makes the filter diverge — the resulting NaN recirculates through the
    // reverb tank's feedback path and silences all output until reset. Cap f at
    // 90% of the stability limit so no parameter setting can blow the tank up.
    const float f_max = 0.9f * (-q + std::sqrt(q * q + 4.0f));
    if(f > f_max) f = f_max;
    if(f < 0.0f)  f = 0.0f;
}

void DattorroReverb::ConfigureSize_(float size)
{
    tank_allpass_[0].Clear();
    tank_allpass_[1].Clear();
    tank_allpass_[2].Clear();
    tank_allpass_[3].Clear();
    tank_allpass_[0].SetLength(static_cast<size_t>(0.020f * sample_rate_ * size));
    tank_allpass_[1].SetLength(static_cast<size_t>(0.060f * sample_rate_ * size));
    tank_allpass_[2].SetLength(static_cast<size_t>(0.030f * sample_rate_ * size));
    tank_allpass_[3].SetLength(static_cast<size_t>(0.089f * sample_rate_ * size));
    tank_allpass_[0].SetFeedback(kDensity);
    tank_allpass_[1].SetFeedback(0.5f);
    tank_allpass_[2].SetFeedback(kDensity);
    tank_allpass_[3].SetFeedback(0.5f);
    tank_allpass_[0].SetIndex(0, 0, 0, 0);
    tank_allpass_[1].SetIndex(0,
                              static_cast<size_t>(0.006f * sample_rate_ * size),
                              static_cast<size_t>(0.041f * sample_rate_ * size),
                              0);
    tank_allpass_[2].SetIndex(0, 0, 0, 0);
    tank_allpass_[3].SetIndex(0,
                              static_cast<size_t>(0.031f * sample_rate_ * size),
                              static_cast<size_t>(0.011f * sample_rate_ * size),
                              0);

    tank_delay_[0].Clear();
    tank_delay_[1].Clear();
    tank_delay_[2].Clear();
    tank_delay_[3].Clear();
    tank_delay_[0].SetLength(static_cast<size_t>(0.15f * sample_rate_ * size));
    tank_delay_[1].SetLength(static_cast<size_t>(0.12f * sample_rate_ * size));
    tank_delay_[2].SetLength(static_cast<size_t>(0.14f * sample_rate_ * size));
    tank_delay_[3].SetLength(static_cast<size_t>(0.11f * sample_rate_ * size));
    tank_delay_[0].SetIndex(0,
                            static_cast<size_t>(0.067f * sample_rate_ * size),
                            static_cast<size_t>(0.011f * sample_rate_ * size),
                            static_cast<size_t>(0.121f * sample_rate_ * size));
    tank_delay_[1].SetIndex(0,
                            static_cast<size_t>(0.036f * sample_rate_ * size),
                            static_cast<size_t>(0.089f * sample_rate_ * size),
                            0);
    tank_delay_[2].SetIndex(0,
                            static_cast<size_t>(0.0089f * sample_rate_ * size),
                            static_cast<size_t>(0.099f * sample_rate_ * size),
                            0);
    tank_delay_[3].SetIndex(0,
                            static_cast<size_t>(0.067f * sample_rate_ * size),
                            static_cast<size_t>(0.0041f * sample_rate_ * size),
                            0);
}

void DattorroReverb::Clear_()
{
    control_rate_counter_ = 0;

    // Input bandwidth one-pole LPF: reset state, seed the fixed coefficient.
    bw_yl_ = 0.0f;
    bw_yr_ = 0.0f;
    bw_g_  = OnePoleG_(kInputBandwidthHz, sample_rate_);

    damping_filter_[0].SetSampleRate(sample_rate_);
    damping_filter_[1].SetSampleRate(sample_rate_);
    damping_filter_[0].Reset();
    damping_filter_[1].Reset();
    damping_filter_[0].Resonance(0.0f);
    damping_filter_[1].Resonance(0.0f);
    damping_filter_[0].Type(StateVariable::LOWPASS);
    damping_filter_[1].Type(StateVariable::LOWPASS);

    // Post-tank output one-pole LPF (outside the feedback loop): carries the
    // heavy darkening without affecting tail length. Reset its state.
    out_lpf_yl_ = 0.0f;
    out_lpf_yr_ = 0.0f;

    predelay_.Reset();
    predelay_.SetDelay(static_cast<size_t>(0));
    predelay_r_.Reset();
    predelay_r_.SetDelay(static_cast<size_t>(0));

    chorus_l_.Reset();
    chorus_r_.Reset();
    chorus_delay_l_ = kChorusCenterSamples;
    chorus_delay_r_ = kChorusCenterSamples;

    input_skipping_ = false; // start in full mode; the tail skip re-arms itself
    hr_prev_wetL_ = 0.0f;
    hr_prev_wetR_ = 0.0f;
    reverb_damp_cached_ = -1.0f; // force damping-coeff recompute + SVF re-apply

    allpass_[0].Clear();
    allpass_[1].Clear();
    allpass_[2].Clear();
    allpass_[3].Clear();
    allpass_[0].SetLength(static_cast<size_t>(0.0048f * sample_rate_));
    allpass_[1].SetLength(static_cast<size_t>(0.0036f * sample_rate_));
    allpass_[2].SetLength(static_cast<size_t>(0.0127f * sample_rate_));
    allpass_[3].SetLength(static_cast<size_t>(0.0093f * sample_rate_));
    allpass_[0].SetFeedback(0.75f);
    allpass_[1].SetFeedback(0.75f);
    allpass_[2].SetFeedback(0.625f);
    allpass_[3].SetFeedback(0.625f);

    allpass_r_[0].Clear();
    allpass_r_[1].Clear();
    allpass_r_[2].Clear();
    allpass_r_[3].Clear();
    allpass_r_[0].SetLength(static_cast<size_t>(0.0048f * sample_rate_));
    allpass_r_[1].SetLength(static_cast<size_t>(0.0036f * sample_rate_));
    allpass_r_[2].SetLength(static_cast<size_t>(0.0127f * sample_rate_));
    allpass_r_[3].SetLength(static_cast<size_t>(0.0093f * sample_rate_));
    allpass_r_[0].SetFeedback(0.75f);
    allpass_r_[1].SetFeedback(0.75f);
    allpass_r_[2].SetFeedback(0.625f);
    allpass_r_[3].SetFeedback(0.625f);

    ConfigureSize_(kSize);

    early_delay_[0].Clear();
    early_delay_[1].Clear();
    early_delay_[0].SetLength(static_cast<size_t>(0.089f * sample_rate_));
    early_delay_[0].SetIndex(0,
                             static_cast<size_t>(0.0199f * sample_rate_),
                             static_cast<size_t>(0.0219f * sample_rate_),
                             static_cast<size_t>(0.0354f * sample_rate_),
                             static_cast<size_t>(0.0389f * sample_rate_),
                             static_cast<size_t>(0.0414f * sample_rate_),
                             static_cast<size_t>(0.0692f * sample_rate_),
                             0);
    early_delay_[1].SetLength(static_cast<size_t>(0.069f * sample_rate_));
    early_delay_[1].SetIndex(0,
                             static_cast<size_t>(0.0099f * sample_rate_),
                             static_cast<size_t>(0.0110f * sample_rate_),
                             static_cast<size_t>(0.0182f * sample_rate_),
                             static_cast<size_t>(0.0189f * sample_rate_),
                             static_cast<size_t>(0.0213f * sample_rate_),
                             static_cast<size_t>(0.0431f * sample_rate_),
                             0);

    previous_left_tank_  = 0.0f;
    previous_right_tank_ = 0.0f;

    // P3: ensure cached feedback params are in sync with current `decay_` on
    // Clear_, in case the reverb is re-prepared mid-session (rather than only
    // via Init). Clear_ is also called from Init so this mirrors the Init seed.
    current_decay_    = kDecayFbMin + (kDecayFbMax - kDecayFbMin) * decay_;
    current_density2_ = current_decay_ + 0.15f;
    if(current_density2_ > 0.5f)
        current_density2_ = 0.5f;
    if(current_density2_ < 0.25f)
        current_density2_ = 0.25f;
}

ADSR2_ITCM_TEXT float DattorroReverb::ProcessPredelayLine_(daisysp::DelayLine<float, kPredelayMax>& line,
                                           float                            input)
{
    const float output = line.Read();
    line.Write(input);
    return output;
}

ADSR2_ITCM_TEXT void DattorroReverb::ApplyChorus_(float& wetL, float& wetR, float mix, float makeup)
{
    // Always write so the delay lines keep fresh history (cheap), but skip the
    // read + cross-couple + blend when the chorus is effectively off (mod ~ 0)
    // — that work is pure overhead at mix = 0.
    chorus_l_.Write(wetL);
    chorus_r_.Write(wetR);
    if(mix < 1.0e-4f)
        return;
    const float chL = chorus_l_.Read(chorus_delay_l_);
    const float chR = chorus_r_.Read(chorus_delay_r_);
    // Cross-couple the detuned voices across channels for an ensemble width:
    // each side takes the opposite line's wandering voice (kChorusCross = 1 →
    // full swap).
    const float voiceL = chL + (chR - chL) * kChorusCross;
    const float voiceR = chR + (chL - chR) * kChorusCross;
    // Keep the dry wet at FULL level and ADD the voice (vs. crossfading): the
    // dry stays present so the mid comb is shallower (less hollow), and the
    // level doesn't collapse. The voice is decorrelated from the dry, so power
    // adds as 1 + mix^2; `makeup` = 1/sqrt(1 + mix^2) (computed once per block by
    // the caller) holds total level steady as `mod` opens up.
    wetL = (wetL + voiceL * mix) * makeup;
    wetR = (wetR + voiceR * mix) * makeup;
}

void DattorroReverb::Init()
{
    // Engine rate. kReverbRateDiv == 2 (REVERB_HALF_RATE) runs the whole reverb
    // at 24 kHz; ConfigureSize_/filters/predelay/control_rate all derive from
    // this, so room times are preserved. ProcessBlock decimates in / interps out.
    sample_rate_ = kSampleRate / static_cast<float>(kReverbRateDiv);

    allpass_[0].Init(g_dattorro_input_ap1_buf, kInputAp1Max);
    allpass_[1].Init(g_dattorro_input_ap2_buf, kInputAp2Max);
    allpass_[2].Init(g_dattorro_input_ap3_buf, kInputAp3Max);
    allpass_[3].Init(g_dattorro_input_ap4_buf, kInputAp4Max);
    allpass_r_[0].Init(g_dattorro_input_ap1_r_buf, kInputAp1Max);
    allpass_r_[1].Init(g_dattorro_input_ap2_r_buf, kInputAp2Max);
    allpass_r_[2].Init(g_dattorro_input_ap3_r_buf, kInputAp3Max);
    allpass_r_[3].Init(g_dattorro_input_ap4_r_buf, kInputAp4Max);
    tank_allpass_[0].Init(g_dattorro_tank_ap1_buf, kTankAp1Max);
    tank_allpass_[1].Init(g_dattorro_tank_ap2_buf, kTankAp2Max);
    tank_allpass_[2].Init(g_dattorro_tank_ap3_buf, kTankAp3Max);
    tank_allpass_[3].Init(g_dattorro_tank_ap4_buf, kTankAp4Max);
    tank_delay_[0].Init(tank_delay1_buf_, kTankDelay1Max);
    tank_delay_[1].Init(tank_delay2_buf_, kTankDelay2Max);
    tank_delay_[2].Init(tank_delay3_buf_, kTankDelay3Max);
    tank_delay_[3].Init(tank_delay4_buf_, kTankDelay4Max);
    early_delay_[0].Init(early_delay_l_buf_, kEarlyDelayLMax);
    early_delay_[1].Init(early_delay_r_buf_, kEarlyDelayRMax);

    predelay_.Init();
    predelay_r_.Init();

    control_rate_ = static_cast<uint32_t>(sample_rate_ / 1000.0f);
    if(control_rate_ == 0)
        control_rate_ = 1;

    // Input bandwidth one-pole LPF: reset state, seed the fixed coefficient.
    bw_yl_ = 0.0f;
    bw_yr_ = 0.0f;
    bw_g_  = OnePoleG_(kInputBandwidthHz, sample_rate_);

    damping_filter_[0].SetSampleRate(sample_rate_);
    damping_filter_[1].SetSampleRate(sample_rate_);
    damping_filter_[0].Resonance(0.0f);
    damping_filter_[1].Resonance(0.0f);
    damping_filter_[0].Type(StateVariable::LOWPASS);
    damping_filter_[1].Type(StateVariable::LOWPASS);

    // Post-tank one-pole darkening LPF: reset state, seed coefficient bright so
    // the first block (before the first control-rate update) runs open.
    out_lpf_yl_ = 0.0f;
    out_lpf_yr_ = 0.0f;
    out_lpf_g_  = OnePoleG_(kOutLpfBright, sample_rate_);

    // Both chorus LFOs are ticked once per control-rate block (~1 kHz), so the
    // effective LFO rate is SetFreq / control_rate_. Scale accordingly.
    oscillator_.Init(sample_rate_);
    oscillator_.SetWaveform(daisysp::Oscillator::WAVE_SIN);
    oscillator_.SetFreq(kChorusRateAHz * static_cast<float>(control_rate_));
    oscillator_.SetAmp(1.0f);

    oscillator2_.Init(sample_rate_);
    oscillator2_.SetWaveform(daisysp::Oscillator::WAVE_SIN);
    oscillator2_.SetFreq(kChorusRateBHz * static_cast<float>(control_rate_));
    oscillator2_.SetAmp(1.0f);

    chorus_l_.Init();
    chorus_r_.Init();
    chorus_delay_l_ = kChorusCenterSamples;
    chorus_delay_r_ = kChorusCenterSamples;

    input_skipping_ = false; // start in full mode; the tail skip re-arms itself
    hr_prev_wetL_ = 0.0f;
    hr_prev_wetR_ = 0.0f;
    reverb_damp_cached_ = -1.0f; // force damping-coeff recompute + SVF re-apply

    SetDamping(0.0f);
    SetDecay(1.0f);
    SetWetDry(0.5f);
    SetMod(0.0f);

    // P3: seed cached feedback params from `decay_` so the first block's
    // per-sample reads have correct values before the first control-rate
    // update fires (which won't happen until control_rate_counter_ reaches
    // control_rate_, i.e. sample 48).
    current_decay_    = kDecayFbMin + (kDecayFbMax - kDecayFbMin) * decay_;
    current_density2_ = current_decay_ + 0.15f;
    if(current_density2_ > 0.5f)
        current_density2_ = 0.5f;
    if(current_density2_ < 0.25f)
        current_density2_ = 0.25f;

    Clear_();
    SetPredelay(0.0f);
}

void DattorroReverb::SetPredelay(float ms)
{
    if(ms < 0.0f)
        ms = 0.0f;
    const float max_ms = (static_cast<float>(kPredelayMax - 1) * 1000.0f) / sample_rate_;
    if(ms > max_ms)
        ms = max_ms;
    predelay_ms_ = ms;
    predelay_base_samples_ = static_cast<size_t>(predelay_ms_ * (sample_rate_ / 1000.0f));
    predelay_.SetDelay(predelay_base_samples_);
    predelay_r_.SetDelay(predelay_base_samples_);
}

void DattorroReverb::SetDamping(float value)
{
    damping_ = Clamp01_(value);
}

void DattorroReverb::SetDecay(float value)
{
    decay_ = Clamp01_(value);
}

void DattorroReverb::SetWetDry(float value)
{
    out_gain_ = Clamp01_(value);
}

void DattorroReverb::SetMod(float value)
{
    mod_ = Clamp01_(value);
}

ADSR2_ITCM_TEXT void DattorroReverb::Process(const float inL,
                             const float inR,
                             float&      outL,
                             float&      outR)
{
    const float left  = inL;
    const float right = inR;

    if(control_rate_counter_ >= control_rate_)
    {
        control_rate_counter_ = 0;

        // P3: hoist decay/density2/tank-feedback updates to the 1 kHz
        // control-rate block. These depend only on `damping_` and `decay_`,
        // which are already smoothed once per audio block (~1 kHz) by the
        // engine, so updating feedback at 1 kHz rather than 48 kHz is
        // sonically transparent while removing 48x redundant work per block.
        const float damping_param   = damping_;
        current_decay_    = kDecayFbMin + (kDecayFbMax - kDecayFbMin) * decay_;
        current_density2_ = current_decay_ + 0.15f;
        if(current_density2_ > 0.5f)
            current_density2_ = 0.5f;
        if(current_density2_ < 0.25f)
            current_density2_ = 0.25f;

        tank_allpass_[0].SetFeedback(kDensity);
        tank_allpass_[1].SetFeedback(current_density2_);
        tank_allpass_[2].SetFeedback(kDensity);
        tank_allpass_[3].SetFeedback(current_density2_);

        const float damp_cutoff = kDampCutoffBright
            * std::pow(kDampCutoffDark / kDampCutoffBright, damping_param);
        const float out_lpf_cutoff = kOutLpfBright
            * std::pow(kOutLpfDark / kOutLpfBright, damping_param);
        damping_filter_[0].Frequency(damp_cutoff);
        damping_filter_[1].Frequency(damp_cutoff);
        out_lpf_g_ = OnePoleG_(out_lpf_cutoff, sample_rate_);

        // Stereo chorus LFO update (post-tank chorus, applied per-sample below).
        // Two slow incommensurate LFOs mixed differently per channel decorrelate
        // the L/R sweep for a wide, swirly image. Centred so the delay stays > 0.
        const float lfo_a = oscillator_.Process();  // [-1..1]
        const float lfo_b = oscillator2_.Process(); // [-1..1]
        const float lfo_l = 0.6f * lfo_a + 0.4f * lfo_b;
        const float lfo_r = 0.6f * lfo_b - 0.4f * lfo_a;
        chorus_delay_l_   = kChorusCenterSamples + kChorusDepthSamples * lfo_l;
        chorus_delay_r_   = kChorusCenterSamples + kChorusDepthSamples * lfo_r;
    }
    ++control_rate_counter_;

    bw_yl_ += bw_g_ * (left - bw_yl_);
    bw_yr_ += bw_g_ * (right - bw_yr_);
    const float bandwidthLeft  = bw_yl_;
    const float bandwidthRight = bw_yr_;

    const float earlyReflectionsL =
        static_cast<float>(early_delay_[0].Process(bandwidthLeft * 0.5f + bandwidthRight * 0.3f)
                         + early_delay_[0].GetIndex(2) * 0.6f
                         + early_delay_[0].GetIndex(3) * 0.4f
                         + early_delay_[0].GetIndex(4) * 0.3f
                         + early_delay_[0].GetIndex(5) * 0.3f
                         + early_delay_[0].GetIndex(6) * 0.1f
                         + early_delay_[0].GetIndex(7) * 0.1f
                         + (bandwidthLeft * 0.4f + bandwidthRight * 0.2f) * 0.5f);

    const float earlyReflectionsR =
        static_cast<float>(early_delay_[1].Process(bandwidthLeft * 0.3f + bandwidthRight * 0.5f)
                         + early_delay_[1].GetIndex(2) * 0.6f
                         + early_delay_[1].GetIndex(3) * 0.4f
                         + early_delay_[1].GetIndex(4) * 0.3f
                         + early_delay_[1].GetIndex(5) * 0.3f
                         + early_delay_[1].GetIndex(6) * 0.1f
                         + early_delay_[1].GetIndex(7) * 0.1f
                         + (bandwidthLeft * 0.2f + bandwidthRight * 0.4f) * 0.5f);

    // Step 2: stereo predelay + input diffuser (L/R parallel chains into the tank).
    float smearedL = ProcessPredelayLine_(predelay_, bandwidthLeft);
    float smearedR = ProcessPredelayLine_(predelay_r_, bandwidthRight);
    for(int i = 0; i < 4; ++i)
        smearedL = allpass_[i].Process(smearedL);
    for(int i = 0; i < 4; ++i)
        smearedR = allpass_r_[i].Process(smearedR);

    const float tankInL = smearedL;
    const float tankInR = smearedR;

    float leftTank = tank_allpass_[0].Process(tankInL + previous_right_tank_);
    leftTank = tank_delay_[0].Process(leftTank);
    leftTank = damping_filter_[0].Process(leftTank);
    leftTank = tank_allpass_[1].Process(leftTank);
    leftTank = tank_delay_[1].Process(leftTank);

    float rightTank = tank_allpass_[2].Process(tankInR + previous_left_tank_);
    rightTank = tank_delay_[2].Process(rightTank);
    rightTank = damping_filter_[1].Process(rightTank);
    rightTank = tank_allpass_[3].Process(rightTank);
    rightTank = tank_delay_[3].Process(rightTank);

    previous_left_tank_  = leftTank * current_decay_;
    previous_right_tank_ = rightTank * current_decay_;

    float accumulatorL = static_cast<float>((0.6f * tank_delay_[2].GetIndex(1))
                                          + (0.6f * tank_delay_[2].GetIndex(2))
                                          - (0.6f * tank_allpass_[3].GetIndex(1))
                                          + (0.6f * tank_delay_[3].GetIndex(1))
                                          - (0.6f * tank_delay_[0].GetIndex(1))
                                          - (0.6f * tank_allpass_[1].GetIndex(1))
                                          - (0.6f * tank_delay_[1].GetIndex(1)));

    float accumulatorR = static_cast<float>((0.6f * tank_delay_[0].GetIndex(2))
                                          + (0.6f * tank_delay_[0].GetIndex(3))
                                          - (0.6f * tank_allpass_[1].GetIndex(2))
                                          + (0.6f * tank_delay_[1].GetIndex(2))
                                          - (0.6f * tank_delay_[2].GetIndex(3))
                                          - (0.6f * tank_allpass_[3].GetIndex(2))
                                          - (0.6f * tank_delay_[3].GetIndex(2)));

    accumulatorL = (accumulatorL * kEarlyMix)
                 + ((1.0f - kEarlyMix) * earlyReflectionsL);
    accumulatorR = (accumulatorR * kEarlyMix)
                 + ((1.0f - kEarlyMix) * earlyReflectionsR);

    float wetL = accumulatorL * kGain;
    float wetR = accumulatorR * kGain;

    // True stereo chorus on the wet tail (`mod_` = chorus amount).
    const float chorus_mix_p    = mod_ * kChorusMaxMix;
    const float chorus_makeup_p = 1.0f / std::sqrt(1.0f + chorus_mix_p * chorus_mix_p);
    ApplyChorus_(wetL, wetR, chorus_mix_p, chorus_makeup_p);

    // Post-tank darkening (outside the feedback loop — no effect on tail length).
    out_lpf_yl_ += out_lpf_g_ * (wetL - out_lpf_yl_);
    out_lpf_yr_ += out_lpf_g_ * (wetR - out_lpf_yr_);
    wetL = out_lpf_yl_;
    wetR = out_lpf_yr_;

    outL = inL + wetL * out_gain_;
    outR = inR + wetR * out_gain_;
}

ADSR2_ITCM_TEXT void DattorroReverb::RenderWet_(const float* inL,
                                const float* inR,
                                size_t       n,
                                float*       wetOutL,
                                float*       wetOutR)
{
    // Safety net: if the tank feedback state ever goes non-finite (NaN/Inf),
    // it would recirculate forever and silence all output until a power cycle.
    // Re-initialise the tank so it recovers within one block instead. Cheap:
    // two float checks per block on the normal path.
    if(!std::isfinite(previous_left_tank_) || !std::isfinite(previous_right_tank_))
        Clear_();

    // ---- Block-constant scalars (hoisted out of the inner loop) ----
    // Damping-derived coeffs are a function of damping_ only (2 pow for the
    // damping/out-LPF cutoffs, an exp in OnePoleG_, and the 2 SVF sin via
    // Frequency). Recompute them — and re-apply the SVF cutoff — only when
    // damping_ actually moves; reuse the cached values otherwise. damping_ is
    // constant within a block, so applying Frequency here (vs the in-loop
    // control-rate tick) is equivalent. (control-rate caching, not gating.)
    if(damping_ != reverb_damp_cached_)
    {
        const float damp_cutoff = kDampCutoffBright
            * std::pow(kDampCutoffDark / kDampCutoffBright, damping_);
        const float out_lpf_cutoff = kOutLpfBright
            * std::pow(kOutLpfDark / kOutLpfBright, damping_);
        reverb_out_lpf_g_cached_ = OnePoleG_(out_lpf_cutoff, sample_rate_);
        damping_filter_[0].Frequency(damp_cutoff);
        damping_filter_[1].Frequency(damp_cutoff);
        reverb_damp_cached_ = damping_;
    }
    const float out_lpf_g       = reverb_out_lpf_g_cached_;
    const float decay           = kDecayFbMin + (kDecayFbMax - kDecayFbMin) * decay_;
    float       density2_tmp    = decay + 0.15f;
    if(density2_tmp > 0.5f)
        density2_tmp = 0.5f;
    if(density2_tmp < 0.25f)
        density2_tmp = 0.25f;
    const float    density2      = density2_tmp;
    const float    mod_val       = mod_;
    const uint32_t crate         = control_rate_;

    // Chorus blend (mod): block-constant wet/dry of the post-tank chorus voice.
    // mod = 0 → bypass (untouched wet); mod = 1 → kChorusMaxMix. The makeup
    // (1/sqrt(1+mix^2)) is hoisted here so the sqrt runs once per block, not
    // per sample.
    const float    chorus_mix    = mod_val * kChorusMaxMix;
    const float    chorus_makeup = 1.0f / std::sqrt(1.0f + chorus_mix * chorus_mix);

    // ---- Lift redundant per-sample tank-allpass SetFeedback calls ----
    // `kDensity` is a compile-time constant, and `density2` is derived from
    // the block-constant `decay`, so feedback only needs to be written once
    // per block. The per-sample versions in Process() repeat the same stores
    // 48 times per block; here we write them once.
    tank_allpass_[0].SetFeedback(kDensity);
    tank_allpass_[1].SetFeedback(density2);
    tank_allpass_[2].SetFeedback(kDensity);
    tank_allpass_[3].SetFeedback(density2);

    // ---- Hoisted per-block state (written back to members once at the end) ----
    uint32_t ctr        = control_rate_counter_;
    float    prev_left  = previous_left_tank_;
    float    prev_right = previous_right_tank_;
    float    out_lpf_yl = out_lpf_yl_;
    float    out_lpf_yr = out_lpf_yr_;
    float    bw_yl      = bw_yl_;
    float    bw_yr      = bw_yr_;
    const float bw_g    = bw_g_;

    // Tail input-skip decision (see kInputSilenceEps). Scan this block's input
    // for any signal; if present, the input section must run. `skip` is held
    // across blocks once the section has flushed, and only released here when
    // input returns — so the long silent tail runs without the input half.
    float in_max = 0.0f;
    for(size_t i = 0; i < n; ++i)
    {
        const float al = std::fabs(inL[i]);
        const float ar = std::fabs(inR[i]);
        if(al > in_max)
            in_max = al;
        if(ar > in_max)
            in_max = ar;
    }
    const bool in_silent = (in_max < kInputSilenceEps);
    if(!in_silent)
        input_skipping_ = false;
    const bool skip     = input_skipping_;
    float      sec_peak = 0.0f; // peak of the input-section output this block

    for(size_t i = 0; i < n; ++i)
    {
        const float left  = inL[i];
        const float right = inR[i];

        if(ctr >= crate)
        {
            ctr = 0;
            // (damping_filter_ cutoff is applied at block-top, only when damping_
            // moves; the chorus LFO must keep advancing every control-rate tick.)

            // Stereo chorus LFO update (post-tank chorus applied per-sample
            // below): two slow incommensurate LFOs mixed differently per
            // channel decorrelate the L/R sweep for a wide, swirly image.
            const float lfo_a = oscillator_.Process();  // [-1..1]
            const float lfo_b = oscillator2_.Process(); // [-1..1]
            const float lfo_l = 0.6f * lfo_a + 0.4f * lfo_b;
            const float lfo_r = 0.6f * lfo_b - 0.4f * lfo_a;
            chorus_delay_l_   = kChorusCenterSamples + kChorusDepthSamples * lfo_l;
            chorus_delay_r_   = kChorusCenterSamples + kChorusDepthSamples * lfo_r;
        }
        ++ctr;

        float tankInL, tankInR;
        float earlyReflectionsL, earlyReflectionsR;
        if(skip)
        {
            // Tail: input silent and input section flushed. Feed the tank zero
            // and skip the bandwidth filters, predelay, input allpasses and
            // early reflections entirely (they would only output zeros).
            tankInL = 0.0f;
            tankInR = 0.0f;
            earlyReflectionsL = 0.0f;
            earlyReflectionsR = 0.0f;
        }
        else
        {
            bw_yl += bw_g * (left - bw_yl);
            bw_yr += bw_g * (right - bw_yr);
            const float bandwidthLeft  = bw_yl;
            const float bandwidthRight = bw_yr;

            earlyReflectionsL =
                static_cast<float>(early_delay_[0].Process(bandwidthLeft * 0.5f + bandwidthRight * 0.3f)
                                 + early_delay_[0].GetIndex(2) * 0.6f
                                 + early_delay_[0].GetIndex(3) * 0.4f
                                 + early_delay_[0].GetIndex(4) * 0.3f
                                 + early_delay_[0].GetIndex(5) * 0.3f
                                 + early_delay_[0].GetIndex(6) * 0.1f
                                 + early_delay_[0].GetIndex(7) * 0.1f
                                 + (bandwidthLeft * 0.4f + bandwidthRight * 0.2f) * 0.5f);

            earlyReflectionsR =
                static_cast<float>(early_delay_[1].Process(bandwidthLeft * 0.3f + bandwidthRight * 0.5f)
                                 + early_delay_[1].GetIndex(2) * 0.6f
                                 + early_delay_[1].GetIndex(3) * 0.4f
                                 + early_delay_[1].GetIndex(4) * 0.3f
                                 + early_delay_[1].GetIndex(5) * 0.3f
                                 + early_delay_[1].GetIndex(6) * 0.1f
                                 + early_delay_[1].GetIndex(7) * 0.1f
                                 + (bandwidthLeft * 0.2f + bandwidthRight * 0.4f) * 0.5f);

            float smearedL = ProcessPredelayLine_(predelay_, bandwidthLeft);
            float smearedR = ProcessPredelayLine_(predelay_r_, bandwidthRight);
            for(int j = 0; j < 4; ++j)
                smearedL = allpass_[j].Process(smearedL);
            for(int j = 0; j < 4; ++j)
                smearedR = allpass_r_[j].Process(smearedR);

            tankInL = smearedL;
            tankInR = smearedR;

            // Track the loudest input-section output this block so we know when
            // it has emptied (and the section can be skipped next block).
            float p = std::fabs(tankInL);
            float q = std::fabs(tankInR);
            if(q > p)
                p = q;
            q = std::fabs(earlyReflectionsL);
            if(q > p)
                p = q;
            q = std::fabs(earlyReflectionsR);
            if(q > p)
                p = q;
            if(p > sec_peak)
                sec_peak = p;
        }

        float leftTank = tank_allpass_[0].Process(tankInL + prev_right);
        leftTank       = tank_delay_[0].Process(leftTank);
        leftTank       = damping_filter_[0].Process(leftTank);
        leftTank       = tank_allpass_[1].Process(leftTank);
        leftTank       = tank_delay_[1].Process(leftTank);

        float rightTank = tank_allpass_[2].Process(tankInR + prev_left);
        rightTank       = tank_delay_[2].Process(rightTank);
        rightTank       = damping_filter_[1].Process(rightTank);
        rightTank       = tank_allpass_[3].Process(rightTank);
        rightTank       = tank_delay_[3].Process(rightTank);

        prev_left  = leftTank * decay;
        prev_right = rightTank * decay;

        float accumulatorL = static_cast<float>((0.6f * tank_delay_[2].GetIndex(1))
                                              + (0.6f * tank_delay_[2].GetIndex(2))
                                              - (0.6f * tank_allpass_[3].GetIndex(1))
                                              + (0.6f * tank_delay_[3].GetIndex(1))
                                              - (0.6f * tank_delay_[0].GetIndex(1))
                                              - (0.6f * tank_allpass_[1].GetIndex(1))
                                              - (0.6f * tank_delay_[1].GetIndex(1)));

        float accumulatorR = static_cast<float>((0.6f * tank_delay_[0].GetIndex(2))
                                              + (0.6f * tank_delay_[0].GetIndex(3))
                                              - (0.6f * tank_allpass_[1].GetIndex(2))
                                              + (0.6f * tank_delay_[1].GetIndex(2))
                                              - (0.6f * tank_delay_[2].GetIndex(3))
                                              - (0.6f * tank_allpass_[3].GetIndex(2))
                                              - (0.6f * tank_delay_[3].GetIndex(2)));

        accumulatorL = (accumulatorL * kEarlyMix)
                     + ((1.0f - kEarlyMix) * earlyReflectionsL);
        accumulatorR = (accumulatorR * kEarlyMix)
                     + ((1.0f - kEarlyMix) * earlyReflectionsR);

        float wetL = accumulatorL * kGain;
        float wetR = accumulatorR * kGain;

        // True stereo chorus on the wet tail (`mod_` = chorus amount).
        ApplyChorus_(wetL, wetR, chorus_mix, chorus_makeup);

        // Post-tank darkening (one-pole, outside the feedback loop — no effect
        // on tail length).
        out_lpf_yl += out_lpf_g * (wetL - out_lpf_yl);
        out_lpf_yr += out_lpf_g * (wetR - out_lpf_yr);
        wetL = out_lpf_yl;
        wetR = out_lpf_yr;

        // Wet-only output (no dry, no out_gain). ProcessBlock adds the pristine
        // full-rate dry and applies out_gain after any rate conversion.
        wetOutL[i] = wetL;
        wetOutR[i] = wetR;
    }

    // If we ran the full input section this block and both the input and the
    // section output are now silent, the section has flushed — bypass it next
    // block. (When skipping, sec_peak stays 0 and we simply keep skipping until
    // input returns and clears the flag at the top.)
    if(!skip && in_silent && sec_peak < kInputFlushEps)
        input_skipping_ = true;

    control_rate_counter_ = ctr;
    previous_left_tank_   = prev_left;
    previous_right_tank_  = prev_right;
    out_lpf_yl_           = out_lpf_yl;
    out_lpf_yr_           = out_lpf_yr;
    bw_yl_                = bw_yl;
    bw_yr_                = bw_yr;
}

ADSR2_ITCM_TEXT void DattorroReverb::ProcessBlock(const float* inL,
                                  const float* inR,
                                  float*       outL,
                                  float*       outR,
                                  size_t       n)
{
    constexpr size_t kBlockMax = 48; // hardware block size; bounds the scratch
    if(n > kBlockMax)
        n = kBlockMax;
    const float out_gain = out_gain_;

#if REVERB_HALF_RATE
    // Whole reverb at half rate. Decimate the input 2:1 (2-tap average — a gentle
    // anti-alias with a null at Nyquist), run the wet engine over n/2 samples,
    // then linear-interpolate the wet back to full rate and add the pristine
    // full-rate dry. Assumes an even block (hardware n = 48).
    const size_t nH = n / 2u;
    float dinL[kBlockMax / 2], dinR[kBlockMax / 2];
    for(size_t i = 0; i < nH; ++i)
    {
        dinL[i] = 0.5f * (inL[2u * i] + inL[2u * i + 1u]);
        dinR[i] = 0.5f * (inR[2u * i] + inR[2u * i + 1u]);
    }

    float wetHL[kBlockMax / 2], wetHR[kBlockMax / 2];
    RenderWet_(dinL, dinR, nH, wetHL, wetHR);

    // Linear 2x upsample of the wet, continuous across blocks via hr_prev_wet*_.
    float prevL = hr_prev_wetL_;
    float prevR = hr_prev_wetR_;
    for(size_t i = 0; i < nH; ++i)
    {
        const float cL = wetHL[i];
        const float cR = wetHR[i];
        outL[2u * i]      = inL[2u * i]      + (0.5f * (prevL + cL)) * out_gain;
        outL[2u * i + 1u] = inL[2u * i + 1u] + cL * out_gain;
        outR[2u * i]      = inR[2u * i]      + (0.5f * (prevR + cR)) * out_gain;
        outR[2u * i + 1u] = inR[2u * i + 1u] + cR * out_gain;
        prevL = cL;
        prevR = cR;
    }
    hr_prev_wetL_ = prevL;
    hr_prev_wetR_ = prevR;

    // Defensive odd-n tail (never hit at the fixed 48-sample block): dry + last wet.
    if(n & 1u)
    {
        outL[n - 1u] = inL[n - 1u] + prevL * out_gain;
        outR[n - 1u] = inR[n - 1u] + prevR * out_gain;
    }
#else
    float wetL[kBlockMax], wetR[kBlockMax];
    RenderWet_(inL, inR, n, wetL, wetR);
    for(size_t i = 0; i < n; ++i)
    {
        outL[i] = inL[i] + wetL[i] * out_gain;
        outR[i] = inR[i] + wetR[i] * out_gain;
    }
#endif
}
