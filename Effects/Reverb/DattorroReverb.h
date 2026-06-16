#pragma once

#include <cstddef>
#include <cstdint>

#include "daisysp.h"
#include "../../mem_regions.h"

class DattorroReverb
{
  public:
    void Init();
    void Process(const float inL, const float inR, float& outL, float& outR);
    // Block variant: hoists scalar parameter/state members (damping/decay/
    // out_gain/control_rate_counter/previous_*_tank) into stack locals and
    // lifts the redundant per-sample tank-allpass SetFeedback calls outside
    // the hot loop. Produces `outL[i] = inL[i] + wetL*out_gain` just like the
    // per-sample Process, so callers can subtract input to extract wet-only.
    void ProcessBlock(const float* inL, const float* inR, float* outL, float* outR, size_t n);

    void SetPredelay(float ms);
    void SetDamping(float value);
    void SetDecay(float value);
    void SetWetDry(float value);
    void SetMod(float value);

  private:
    struct Allpass
    {
        float*  buf      = nullptr;
        size_t  max_len  = 0;
        size_t  len      = 0;
        size_t  idx      = 0;
        float   feedback = 0.5f;

        void Init(float* b, size_t max_length);
        void Clear();
        void SetLength(size_t length);
        void SetFeedback(float value);
        float Process(float input);
    };

    struct StaticAllpassFourTap
    {
        float*  buf      = nullptr;
        size_t  max_len  = 0;
        size_t  len      = 0;
        size_t  idx1     = 0;
        size_t  idx2     = 0;
        size_t  idx3     = 0;
        size_t  idx4     = 0;
        float   feedback = 0.5f;

        void Init(float* b, size_t max_length);
        void Clear();
        void SetLength(size_t length);
        void SetFeedback(float value);
        void SetIndex(size_t i1, size_t i2, size_t i3, size_t i4);
        float Process(float input);
        float GetIndex(size_t index) const;
    };

    struct StaticDelayLineFourTap
    {
        float*  buf     = nullptr;
        size_t  max_len = 0;
        size_t  len     = 0;
        size_t  idx1    = 0;
        size_t  idx2    = 0;
        size_t  idx3    = 0;
        size_t  idx4    = 0;

        void Init(float* b, size_t max_length);
        void Clear();
        void SetLength(size_t length);
        void SetIndex(size_t i1, size_t i2, size_t i3, size_t i4);
        float Process(float input);
        float GetIndex(size_t index) const;
    };

    struct StaticDelayLineEightTap
    {
        float*  buf     = nullptr;
        size_t  max_len = 0;
        size_t  len     = 0;
        size_t  idx1    = 0;
        size_t  idx2    = 0;
        size_t  idx3    = 0;
        size_t  idx4    = 0;
        size_t  idx5    = 0;
        size_t  idx6    = 0;
        size_t  idx7    = 0;
        size_t  idx8    = 0;

        void Init(float* b, size_t max_length);
        void Clear();
        void SetLength(size_t length);
        void SetIndex(size_t i1,
                      size_t i2,
                      size_t i3,
                      size_t i4,
                      size_t i5,
                      size_t i6,
                      size_t i7,
                      size_t i8);
        float Process(float input);
        float GetIndex(size_t index) const;
    };

    struct StateVariable
    {
        enum FilterType
        {
            LOWPASS,
            HIGHPASS,
            BANDPASS,
            NOTCH,
        };

        float      sample_rate = 44100.0f;
        float      frequency   = 1000.0f;
        float      q           = 2.0f;
        float      f           = 0.0f;
        float      low         = 0.0f;
        float      high        = 0.0f;
        float      band        = 0.0f;
        float      notch       = 0.0f;
        FilterType type        = LOWPASS;

        void  Reset();
        void  SetSampleRate(float value);
        void  Frequency(float value);
        void  Resonance(float value);
        void  Type(FilterType value);
        float Process(float input);

      private:
        void UpdateCoefficient();
    };

    static constexpr float kSampleRate     = 48000.0f;
    static constexpr float kDensity        = 0.5f;
    static constexpr float kSize           = 0.75f;
    static constexpr float kEarlyMix       = 0.75f;
    static constexpr float kGain           = 1.0f;
    static constexpr size_t kInputAp1Max   = 230;
    static constexpr size_t kInputAp2Max   = 172;
    static constexpr size_t kInputAp3Max   = 609;
    static constexpr size_t kInputAp4Max   = 446;
    static constexpr size_t kPredelayMax   = 9600;
    static constexpr size_t kTankAp1Max    = 960;
    static constexpr size_t kTankAp2Max    = 2880;
    static constexpr size_t kTankAp3Max    = 1440;
    static constexpr size_t kTankAp4Max    = 4272;
    static constexpr size_t kTankDelay1Max = 7200;
    static constexpr size_t kTankDelay2Max = 5760;
    static constexpr size_t kTankDelay3Max = 6720;
    static constexpr size_t kTankDelay4Max = 5280;
    static constexpr size_t kEarlyDelayLMax = 4272;
    static constexpr size_t kEarlyDelayRMax = 3312;
    static constexpr size_t kChorusMax      = 1024; // ~21 ms @ 48k; holds center+depth

    static float Clamp01_(float value);

    void  Clear_();
    void  ConfigureSize_(float size);
    float ProcessPredelayLine_(daisysp::DelayLine<float, kPredelayMax>& line, float input);
    // Post-tank stereo chorus: write wet into the L/R chorus lines, read back at
    // the current modulated delay, and blend `mix` of the chorused voice in.
    // `makeup` (1/sqrt(1+mix^2)) is precomputed once per block by the caller.
    void  ApplyChorus_(float& wetL, float& wetR, float mix, float makeup);

    float sample_rate_ = kSampleRate;
    float damping_     = 0.0f;
    float decay_       = 1.0f;
    float out_gain_    = 0.5f;
    float predelay_ms_ = 0.0f;
    float mod_         = 0.0f;
    size_t predelay_base_samples_ = 0;

    uint32_t control_rate_         = 48;
    uint32_t control_rate_counter_ = 0;

    // P3: cached per-sample reverb feedback parameters, recomputed inside the
    // 1 kHz control-rate block. `current_decay_` mirrors the decay-fader ->
    // feedback map (kDecayFbMin..kDecayFbMax, see DattorroReverb.cpp) and
    // `current_density2_` mirrors the clamped `current_decay_ + 0.15` at the
    // last control update. Per-sample reads these directly instead of
    // recomputing every sample, and `tank_allpass_[].SetFeedback` is updated
    // alongside them. Defaults are seeded again in Init/Clear_ so the first
    // block has valid feedback until the first control update lands at sample
    // 48 (the literal here just matches the default decay_ = 1.0 ceiling).
    float current_decay_    = 0.94f;
    float current_density2_ = 0.5f;

    daisysp::Oscillator                      oscillator_;
    daisysp::Oscillator                      oscillator2_;
    daisysp::DelayLine<float, kPredelayMax> predelay_;
    daisysp::DelayLine<float, kPredelayMax> predelay_r_;
    daisysp::DelayLine<float, kChorusMax>   chorus_l_;
    daisysp::DelayLine<float, kChorusMax>   chorus_r_;
    float chorus_delay_l_ = 0.0f; // current modulated chorus delay (samples)
    float chorus_delay_r_ = 0.0f;

    Allpass               allpass_[4];
    Allpass               allpass_r_[4];
    StaticAllpassFourTap  tank_allpass_[4];
    // Input bandwidth LPF (fixed bright, ~16 kHz). One-pole per channel instead
    // of a 2x-oversampled StateVariable: at this cutoff the audible difference
    // is negligible (a hair brighter) and it drops two oversampled SVFs out of
    // the always-on reverb path. `bw_g_` is the fixed one-pole coefficient.
    float                 bw_yl_ = 0.0f;
    float                 bw_yr_ = 0.0f;
    float                 bw_g_  = 1.0f;
    StateVariable         damping_filter_[2];

    // Post-tank darkening: a cheap one-pole LPF per channel (outside the tank
    // feedback, so no oversampled-SVF stability concern). `out_lpf_g_` is the
    // one-pole coefficient, recomputed at the control rate from the damp knob.
    float out_lpf_yl_ = 0.0f;
    float out_lpf_yr_ = 0.0f;
    float out_lpf_g_  = 1.0f;

    // Tail CPU saver: true once the reverb input has gone silent AND the input
    // section (bandwidth filters / predelay / input allpasses / early
    // reflections) has flushed to zero. While set, ProcessBlock feeds the tank
    // zero and skips that whole section — the long tail rings out at much lower
    // CPU with identical tail length and tone. Cleared instantly when input
    // returns (the skipped section's state sits at ~0, so resuming is seamless).
    bool input_skipping_ = false;
    StaticDelayLineFourTap tank_delay_[4];
    StaticDelayLineEightTap early_delay_[2];

    float previous_left_tank_  = 0.0f;
    float previous_right_tank_ = 0.0f;

    ADSR2_ALIGN32 float tank_delay1_buf_[kTankDelay1Max];
    ADSR2_ALIGN32 float tank_delay2_buf_[kTankDelay2Max];
    ADSR2_ALIGN32 float tank_delay3_buf_[kTankDelay3Max];
    ADSR2_ALIGN32 float tank_delay4_buf_[kTankDelay4Max];
    ADSR2_ALIGN32 float early_delay_l_buf_[kEarlyDelayLMax];
    ADSR2_ALIGN32 float early_delay_r_buf_[kEarlyDelayRMax];
};
