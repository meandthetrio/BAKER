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
// The input bandwidth filter is fixed and bright — it is NOT driven by the
// damping knob (classic Dattorro). The damping knob only darkens the tank
// tail, mapped exponentially between these two cutoffs so the knob travel
// feels perceptually even. The bright end stays within the SVF stability
// limit so the whole range is active (no dead plateau).
constexpr float kInputBandwidthHz = 16000.0f; // fixed bright input LPF
constexpr float kDampCutoffBright  = 11000.0f; // damp = 0 (brightest tail)
constexpr float kDampCutoffDark    = 400.0f;   // damp = 1 (darkest tail)
} // namespace

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

float DattorroReverb::Allpass::Process(float input)
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

float DattorroReverb::StaticAllpassFourTap::Process(float input)
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

float DattorroReverb::StaticDelayLineFourTap::Process(float input)
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

float DattorroReverb::StaticDelayLineEightTap::Process(float input)
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

float DattorroReverb::StateVariable::Process(float input)
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

    bandwidth_filter_[0].SetSampleRate(sample_rate_);
    bandwidth_filter_[1].SetSampleRate(sample_rate_);
    bandwidth_filter_[0].Reset();
    bandwidth_filter_[1].Reset();
    bandwidth_filter_[0].Resonance(0.0f);
    bandwidth_filter_[1].Resonance(0.0f);
    bandwidth_filter_[0].Type(StateVariable::LOWPASS);
    bandwidth_filter_[1].Type(StateVariable::LOWPASS);

    damping_filter_[0].SetSampleRate(sample_rate_);
    damping_filter_[1].SetSampleRate(sample_rate_);
    damping_filter_[0].Reset();
    damping_filter_[1].Reset();
    damping_filter_[0].Resonance(0.0f);
    damping_filter_[1].Resonance(0.0f);
    damping_filter_[0].Type(StateVariable::LOWPASS);
    damping_filter_[1].Type(StateVariable::LOWPASS);

    predelay_.Reset();
    predelay_.SetDelay(static_cast<size_t>(0));
    predelay_r_.Reset();
    predelay_r_.SetDelay(static_cast<size_t>(0));

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
    current_decay_    = (0.7995f * decay_) + 0.005f;
    current_density2_ = current_decay_ + 0.15f;
    if(current_density2_ > 0.5f)
        current_density2_ = 0.5f;
    if(current_density2_ < 0.25f)
        current_density2_ = 0.25f;
}

float DattorroReverb::ProcessPredelayLine_(daisysp::DelayLine<float, kPredelayMax>& line,
                                           float                            input)
{
    const float output = line.Read();
    line.Write(input);
    return output;
}

void DattorroReverb::Init()
{
    sample_rate_ = kSampleRate;

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

    bandwidth_filter_[0].SetSampleRate(sample_rate_);
    bandwidth_filter_[1].SetSampleRate(sample_rate_);
    bandwidth_filter_[0].Resonance(0.0f);
    bandwidth_filter_[1].Resonance(0.0f);
    bandwidth_filter_[0].Type(StateVariable::LOWPASS);
    bandwidth_filter_[1].Type(StateVariable::LOWPASS);

    damping_filter_[0].SetSampleRate(sample_rate_);
    damping_filter_[1].SetSampleRate(sample_rate_);
    damping_filter_[0].Resonance(0.0f);
    damping_filter_[1].Resonance(0.0f);
    damping_filter_[0].Type(StateVariable::LOWPASS);
    damping_filter_[1].Type(StateVariable::LOWPASS);

    oscillator_.Init(sample_rate_);
    oscillator_.SetWaveform(daisysp::Oscillator::WAVE_SIN);
    oscillator_.SetFreq(0.6f * static_cast<float>(control_rate_));
    oscillator_.SetAmp(1.0f);

    SetDamping(0.0f);
    SetDecay(1.0f);
    SetWetDry(0.5f);
    SetMod(0.0f);

    // P3: seed cached feedback params from `decay_` so the first block's
    // per-sample reads have correct values before the first control-rate
    // update fires (which won't happen until control_rate_counter_ reaches
    // control_rate_, i.e. sample 48).
    current_decay_    = (0.7995f * decay_) + 0.005f;
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

void DattorroReverb::Process(const float inL,
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
        current_decay_    = (0.7995f * decay_) + 0.005f;
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
        bandwidth_filter_[0].Frequency(kInputBandwidthHz);
        bandwidth_filter_[1].Frequency(kInputBandwidthHz);
        damping_filter_[0].Frequency(damp_cutoff);
        damping_filter_[1].Frequency(damp_cutoff);

        // Light predelay modulation for “Dattorro-ish” movement.
        // We run the oscillator at the control update rate, so scale its frequency accordingly.
        // Keep this subtle to avoid obvious pitch warble.
        const float hz = 0.6f;
        oscillator_.SetFreq(hz * static_cast<float>(control_rate_));

        // `mod_` scales wet Mid/Side width after the tank (see below); predelay uses fixed depth.
        constexpr float kPredelayModDepthSamples = 5.0f; // ~0.1ms @ 48k
        const float m = oscillator_.Process(); // [-1..1]
        const int32_t mod_samps = static_cast<int32_t>(m * kPredelayModDepthSamples);
        int32_t want = static_cast<int32_t>(predelay_base_samples_) + mod_samps;
        if(want < 0)
            want = 0;
        if(want >= static_cast<int32_t>(kPredelayMax - 1))
            want = static_cast<int32_t>(kPredelayMax - 1);
        const size_t d = static_cast<size_t>(want);
        predelay_.SetDelay(d);
        predelay_r_.SetDelay(d);
    }
    ++control_rate_counter_;

    const float bandwidthLeft  = bandwidth_filter_[0].Process(left);
    const float bandwidthRight = bandwidth_filter_[1].Process(right);

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

    // Wet-only stereo width: boost side of the reverb wet; `mod_` squared for low-end control.
    constexpr float kWetSideMaxExtra = 1.5f;
    const float     wetMid           = 0.5f * (wetL + wetR);
    const float     wetSide          = 0.5f * (wetL - wetR);
    const float     widthCurve       = mod_ * mod_;
    const float     sideBoost        = 1.0f + widthCurve * kWetSideMaxExtra;
    const float     wetSideW         = wetSide * sideBoost;
    wetL                             = wetMid + wetSideW;
    wetR                             = wetMid - wetSideW;

    outL = inL + wetL * out_gain_;
    outR = inR + wetR * out_gain_;
}

void DattorroReverb::ProcessBlock(const float* inL,
                                  const float* inR,
                                  float*       outL,
                                  float*       outR,
                                  size_t       n)
{
    // Safety net: if the tank feedback state ever goes non-finite (NaN/Inf),
    // it would recirculate forever and silence all output until a power cycle.
    // Re-initialise the tank so it recovers within one block instead. Cheap:
    // two float checks per block on the normal path.
    if(!std::isfinite(previous_left_tank_) || !std::isfinite(previous_right_tank_))
        Clear_();

    // ---- Block-constant scalars (hoisted out of the inner loop) ----
    const float damping_param   = damping_;
    // Fixed bright input bandwidth; exponential damping cutoff (see Process).
    const float damp_cutoff     = kDampCutoffBright
        * std::pow(kDampCutoffDark / kDampCutoffBright, damping_param);
    const float decay           = (0.7995f * decay_) + 0.005f;
    float       density2_tmp    = decay + 0.15f;
    if(density2_tmp > 0.5f)
        density2_tmp = 0.5f;
    if(density2_tmp < 0.25f)
        density2_tmp = 0.25f;
    const float    density2      = density2_tmp;
    const float    out_gain      = out_gain_;
    const float    mod_val       = mod_;
    const uint32_t crate         = control_rate_;
    const size_t   predelay_base = predelay_base_samples_;

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

    for(size_t i = 0; i < n; ++i)
    {
        const float left  = inL[i];
        const float right = inR[i];

        if(ctr >= crate)
        {
            ctr = 0;
            bandwidth_filter_[0].Frequency(kInputBandwidthHz);
            bandwidth_filter_[1].Frequency(kInputBandwidthHz);
            damping_filter_[0].Frequency(damp_cutoff);
            damping_filter_[1].Frequency(damp_cutoff);

            const float hz = 0.6f;
            oscillator_.SetFreq(hz * static_cast<float>(crate));

            constexpr float kPredelayModDepthSamples = 5.0f;
            const float     m         = oscillator_.Process();
            const int32_t   mod_samps = static_cast<int32_t>(m * kPredelayModDepthSamples);
            int32_t         want      = static_cast<int32_t>(predelay_base) + mod_samps;
            if(want < 0)
                want = 0;
            if(want >= static_cast<int32_t>(kPredelayMax - 1))
                want = static_cast<int32_t>(kPredelayMax - 1);
            const size_t d = static_cast<size_t>(want);
            predelay_.SetDelay(d);
            predelay_r_.SetDelay(d);
        }
        ++ctr;

        const float bandwidthLeft  = bandwidth_filter_[0].Process(left);
        const float bandwidthRight = bandwidth_filter_[1].Process(right);

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

        float smearedL = ProcessPredelayLine_(predelay_, bandwidthLeft);
        float smearedR = ProcessPredelayLine_(predelay_r_, bandwidthRight);
        for(int j = 0; j < 4; ++j)
            smearedL = allpass_[j].Process(smearedL);
        for(int j = 0; j < 4; ++j)
            smearedR = allpass_r_[j].Process(smearedR);

        const float tankInL = smearedL;
        const float tankInR = smearedR;

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

        constexpr float kWetSideMaxExtra = 1.5f;
        const float     wetMid           = 0.5f * (wetL + wetR);
        const float     wetSide          = 0.5f * (wetL - wetR);
        const float     widthCurve       = mod_val * mod_val;
        const float     sideBoost        = 1.0f + widthCurve * kWetSideMaxExtra;
        const float     wetSideW         = wetSide * sideBoost;
        wetL                             = wetMid + wetSideW;
        wetR                             = wetMid - wetSideW;

        outL[i] = left + wetL * out_gain;
        outR[i] = right + wetR * out_gain;
    }

    control_rate_counter_ = ctr;
    previous_left_tank_   = prev_left;
    previous_right_tank_  = prev_right;
}
