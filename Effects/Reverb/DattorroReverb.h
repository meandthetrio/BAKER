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

    static float Clamp01_(float value);

    void  Clear_();
    void  ConfigureSize_(float size);
    float ProcessPredelay_(float input);

    float sample_rate_ = kSampleRate;
    float damping_     = 0.0f;
    float decay_       = 1.0f;
    float out_gain_    = 0.5f;
    float predelay_ms_ = 0.0f;
    float mod_         = 0.0f;
    size_t predelay_base_samples_ = 0;

    uint32_t control_rate_         = 48;
    uint32_t control_rate_counter_ = 0;

    daisysp::Oscillator                      oscillator_;
    daisysp::DelayLine<float, kPredelayMax> predelay_;

    Allpass               allpass_[4];
    StaticAllpassFourTap  tank_allpass_[4];
    StateVariable         bandwidth_filter_[2];
    StateVariable         damping_filter_[2];
    StaticDelayLineFourTap tank_delay_[4];
    StaticDelayLineEightTap early_delay_[2];

    float previous_left_tank_  = 0.0f;
    float previous_right_tank_ = 0.0f;

    ADSR2_ALIGN32 float input_ap1_buf_[kInputAp1Max];
    ADSR2_ALIGN32 float input_ap2_buf_[kInputAp2Max];
    ADSR2_ALIGN32 float input_ap3_buf_[kInputAp3Max];
    ADSR2_ALIGN32 float input_ap4_buf_[kInputAp4Max];
    ADSR2_ALIGN32 float tank_ap1_buf_[kTankAp1Max];
    ADSR2_ALIGN32 float tank_ap2_buf_[kTankAp2Max];
    ADSR2_ALIGN32 float tank_ap3_buf_[kTankAp3Max];
    ADSR2_ALIGN32 float tank_ap4_buf_[kTankAp4Max];
    ADSR2_ALIGN32 float tank_delay1_buf_[kTankDelay1Max];
    ADSR2_ALIGN32 float tank_delay2_buf_[kTankDelay2Max];
    ADSR2_ALIGN32 float tank_delay3_buf_[kTankDelay3Max];
    ADSR2_ALIGN32 float tank_delay4_buf_[kTankDelay4Max];
    ADSR2_ALIGN32 float early_delay_l_buf_[kEarlyDelayLMax];
    ADSR2_ALIGN32 float early_delay_r_buf_[kEarlyDelayRMax];
};
