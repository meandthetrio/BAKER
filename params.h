#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "keygroups.h"

struct PerformParamsTargets
{
    static constexpr uint8_t kLayerCount = 2;

    float master_level = 1.0f;

    bool  delay_on  = false;
    bool  reverb_on = false;
    bool  sat_on    = false;
    bool  eq_on     = false;

    float delay_mix  = 0.0f;
    float reverb_mix = 0.0f;
    float sat_drive  = 0.0f;
    float sat_mix    = 0.0f;
    float sat_bump   = 0.5f;
    float sat_bit_reso = 0.5f;
    float sat_bit_smpl = 0.5f;
    uint8_t sat_mode = 0; // 0=tape, 1=bit
    float eq_mix          = 0.0f;
    float eq_center_norm  = 0.5f;
    float eq_tilt_db      = 0.0f;
    float eq_q            = 1.2f; // peaking Q both bells, UI range 0.5..1.7
    float delay_time_l   = 0.5f;
    float delay_time_r   = 0.5f;
    float delay_feedback = 0.5f;
    float reverb_pre   = 0.5f;
    float reverb_damp  = 0.5f;
    float reverb_decay = 0.5f;
    float reverb_mod     = 0.0f;
    float lpf_cutoff_hz = 12000.0f;
    float lfo_rate_hz   = 0.0f;
    float lfo_depth     = 0.0f;
    float env_attack_ms = 5.0f;
    float env_decay_ms  = 120.0f;
    float env_amount    = 0.5f;
    float engine_layer_master_level[kLayerCount] = {1.0f, 1.0f}; // 0..2 (UNITY=1)
    float engine_filter_cutoff_hz[kLayerCount] = {20000.0f, 20000.0f};
    float engine_filter_resonance[kLayerCount] = {0.0f, 0.0f}; // 0..1
    float engine_tune_semitones[kLayerCount] = {0.0f, 0.0f};
    float engine_gain_db[kLayerCount] = {0.0f, 0.0f};
    uint8_t engine_drive_mode[kLayerCount] = {0u, 0u}; // 0=odd, 1=even
    bool  engine_loop_mode[kLayerCount] = {false, false};
    float engine_loop_attack_ms[kLayerCount] = {5.0f, 5.0f};
    float engine_loop_decay_ms[kLayerCount] = {20.0f, 20.0f};
    float engine_loop_sustain_level[kLayerCount] = {1.0f, 1.0f}; // 0=-inf, 1=0 dB
    float engine_loop_release_ms[kLayerCount] = {50.0f, 50.0f};
    float engine_loop_crossfade_amount[kLayerCount] = {0.0625f, 0.0625f}; // 0..0.5 of selected length
    float engine_loop_crossfade_shape[kLayerCount] = {0.0f, 0.0f}; // 0=linear, 1=equal-power-like
    uint8_t perform_keyzone_lo_note[kLayerCount] = {48u, 48u}; // C3
    uint8_t perform_keyzone_hi_note[kLayerCount] = {60u, 60u}; // C4
    uint8_t fx_order[4] = {0, 1, 2, 3}; // 0=SAT,1=EQ,2=DELAY,3=REVERB
};

struct PerformParamsCurrent
{
    static constexpr uint8_t kLayerCount = 2;

    float master_level = 1.0f;

    bool  delay_on  = false;
    bool  reverb_on = false;
    bool  sat_on    = false;
    bool  eq_on     = false;

    float delay_mix  = 0.0f;
    float reverb_mix = 0.0f;
    float sat_drive  = 0.0f;
    float sat_mix    = 0.0f;
    float sat_bump   = 0.5f;
    float sat_bit_reso = 0.5f;
    float sat_bit_smpl = 0.5f;
    uint8_t sat_mode = 0; // 0=tape, 1=bit
    float eq_mix          = 0.0f;
    float eq_center_norm  = 0.5f;
    float eq_tilt_db      = 0.0f;
    float eq_q            = 1.2f; // peaking Q both bells, UI range 0.5..1.7
    float delay_time_l   = 0.5f;
    float delay_time_r   = 0.5f;
    float delay_feedback = 0.5f;
    float reverb_pre   = 0.5f;
    float reverb_damp  = 0.5f;
    float reverb_decay = 0.5f;
    float reverb_mod     = 0.0f;
    float lpf_cutoff_hz = 12000.0f;
    float lfo_rate_hz   = 0.0f;
    float lfo_depth     = 0.0f;
    float env_attack_ms = 5.0f;
    float env_decay_ms  = 120.0f;
    float env_amount    = 0.5f;
    float engine_layer_master_level[kLayerCount] = {1.0f, 1.0f}; // 0..2 (UNITY=1)
    float engine_filter_cutoff_hz[kLayerCount] = {20000.0f, 20000.0f};
    float engine_filter_resonance[kLayerCount] = {0.0f, 0.0f}; // 0..1
    float engine_tune_semitones[kLayerCount] = {0.0f, 0.0f};
    float engine_gain_db[kLayerCount] = {0.0f, 0.0f};
    uint8_t engine_drive_mode[kLayerCount] = {0u, 0u}; // 0=odd, 1=even
    bool  engine_loop_mode[kLayerCount] = {false, false};
    float engine_loop_attack_ms[kLayerCount] = {5.0f, 5.0f};
    float engine_loop_decay_ms[kLayerCount] = {20.0f, 20.0f};
    float engine_loop_sustain_level[kLayerCount] = {1.0f, 1.0f}; // 0=-inf, 1=0 dB
    float engine_loop_release_ms[kLayerCount] = {50.0f, 50.0f};
    float engine_loop_crossfade_amount[kLayerCount] = {0.0625f, 0.0625f}; // 0..0.5 of selected length
    float engine_loop_crossfade_shape[kLayerCount] = {0.0f, 0.0f}; // 0=linear, 1=equal-power-like
    uint8_t perform_keyzone_lo_note[kLayerCount] = {48u, 48u}; // C3
    uint8_t perform_keyzone_hi_note[kLayerCount] = {60u, 60u}; // C4
    uint8_t fx_order[4] = {0, 1, 2, 3}; // 0=SAT,1=EQ,2=DELAY,3=REVERB
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
