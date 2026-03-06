#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

struct PerformParamsTargets
{
    static constexpr uint8_t kLayerCount = 2;

    float master_level = 1.0f;

    bool  delay_on  = false;
    bool  reverb_on = false;
    bool  sat_on    = false;
    bool  mod_on    = false;

    float delay_mix  = 0.0f;
    float reverb_mix = 0.0f;
    float sat_drive  = 0.0f;
    float sat_mix    = 0.0f;
    float sat_bump   = 0.5f;
    float sat_bit_reso = 0.5f;
    float sat_bit_smpl = 0.5f;
    uint8_t sat_mode = 0; // 0=tape, 1=bit
    float mod_mix      = 0.0f;
    float mod_rate_hz  = 0.5f;
    float mod_wow      = 0.5f;
    float tape_rate    = 0.5f;
    uint8_t mod_mode   = 0; // 0=chorus, 1=tape
    float delay_time   = 0.5f;
    float delay_feedback = 0.5f;
    float delay_spread = 0.5f;
    float delay_freeze = 0.0f;
    float reverb_pre   = 0.5f;
    float reverb_damp  = 0.5f;
    float reverb_decay = 0.5f;
    bool  reverb_reverse = false;
    float lpf_cutoff_hz = 12000.0f;
    float lfo_rate_hz   = 0.0f;
    float lfo_depth     = 0.0f;
    float env_attack_ms = 5.0f;
    float env_decay_ms  = 120.0f;
    float env_amount    = 0.5f;
    float engine_layer_master_level[kLayerCount] = {1.0f, 1.0f}; // 0..2 (UNITY=1)
    float engine_tune_semitones[kLayerCount] = {0.0f, 0.0f};
    float engine_gain_db[kLayerCount] = {0.0f, 0.0f};
    bool  engine_loop_mode[kLayerCount] = {false, false};
    uint8_t fx_order[4] = {0, 1, 2, 3}; // 0=SAT,1=MOD,2=DELAY,3=REVERB
};

struct PerformParamsCurrent
{
    static constexpr uint8_t kLayerCount = 2;

    float master_level = 1.0f;

    bool  delay_on  = false;
    bool  reverb_on = false;
    bool  sat_on    = false;
    bool  mod_on    = false;

    float delay_mix  = 0.0f;
    float reverb_mix = 0.0f;
    float sat_drive  = 0.0f;
    float sat_mix    = 0.0f;
    float sat_bump   = 0.5f;
    float sat_bit_reso = 0.5f;
    float sat_bit_smpl = 0.5f;
    uint8_t sat_mode = 0; // 0=tape, 1=bit
    float mod_mix      = 0.0f;
    float mod_rate_hz  = 0.5f;
    float mod_wow      = 0.5f;
    float tape_rate    = 0.5f;
    uint8_t mod_mode   = 0; // 0=chorus, 1=tape
    float delay_time   = 0.5f;
    float delay_feedback = 0.5f;
    float delay_spread = 0.5f;
    float delay_freeze = 0.0f;
    float reverb_pre   = 0.5f;
    float reverb_damp  = 0.5f;
    float reverb_decay = 0.5f;
    bool  reverb_reverse = false;
    float lpf_cutoff_hz = 12000.0f;
    float lfo_rate_hz   = 0.0f;
    float lfo_depth     = 0.0f;
    float env_attack_ms = 5.0f;
    float env_decay_ms  = 120.0f;
    float env_amount    = 0.5f;
    float engine_layer_master_level[kLayerCount] = {1.0f, 1.0f}; // 0..2 (UNITY=1)
    float engine_tune_semitones[kLayerCount] = {0.0f, 0.0f};
    float engine_gain_db[kLayerCount] = {0.0f, 0.0f};
    bool  engine_loop_mode[kLayerCount] = {false, false};
    uint8_t fx_order[4] = {0, 1, 2, 3}; // 0=SAT,1=MOD,2=DELAY,3=REVERB
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
