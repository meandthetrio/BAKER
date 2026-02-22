#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

struct PerformParamsTargets
{
    float master_level = 1.0f;

    bool  delay_on  = false;
    bool  reverb_on = false;
    bool  sat_on    = false;

    float delay_mix  = 0.0f;
    float reverb_mix = 0.0f;
    float sat_drive  = 0.0f;
    float lpf_cutoff_hz = 12000.0f;
    float lfo_rate_hz   = 1.0f;
    float lfo_depth     = 0.5f;
    float env_attack_ms = 5.0f;
    float env_decay_ms  = 120.0f;
    float env_amount    = 0.5f;
};

struct PerformParamsCurrent
{
    float master_level = 1.0f;

    bool  delay_on  = false;
    bool  reverb_on = false;
    bool  sat_on    = false;

    float delay_mix  = 0.0f;
    float reverb_mix = 0.0f;
    float sat_drive  = 0.0f;
    float lpf_cutoff_hz = 12000.0f;
    float lfo_rate_hz   = 1.0f;
    float lfo_depth     = 0.5f;
    float env_attack_ms = 5.0f;
    float env_decay_ms  = 120.0f;
    float env_amount    = 0.5f;
};

class Params
{
  public:
    void Init();

    // MAIN LOOP ONLY: edit unpublished targets.
    PerformParamsTargets& EditTargets();

    // MAIN LOOP ONLY: publish edited targets to the audio thread.
    void PublishTargets();

    // MAIN LOOP ONLY: safe view of last published targets (OLED).
    const PerformParamsTargets& TargetsForUI() const;

    // AUDIO THREAD ONLY: smoothed params used for DSP.
    void AudioBlockTick(float sample_rate, size_t block_size);

    PerformParamsCurrent current;

  private:
    PerformParamsTargets targets_buf_[2];
    std::atomic<uint8_t> published_idx_{0};
    uint8_t              write_idx_ = 1;

    // Helper: one-pole smoothing toward target
    static float SmoothToward(float current, float target, float coeff);
};
