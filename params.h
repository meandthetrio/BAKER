#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "express_state.h"
#include "keygroups.h"

static constexpr uint8_t kFxFaderModeSend = 0u;
static constexpr uint8_t kFxFaderModeMix = 1u;
static constexpr uint8_t kReverbFaderModeSend = kFxFaderModeSend;
static constexpr uint8_t kReverbFaderModeMix = kFxFaderModeMix;
static constexpr uint8_t kDelayFaderModeSend = kFxFaderModeSend;
static constexpr uint8_t kDelayFaderModeMix = kFxFaderModeMix;

struct PerformParamsTargets
{
    static constexpr uint8_t kLayerCount = 2;

    float master_level = 0.70f; // OUTPUT VOL default 70 (UNITY=1.0)

    bool  delay_on  = false;
    bool  reverb_on = false;
    bool  sat_on    = false;
    bool  eq_on     = true;

    float delay_mix  = 0.0f;
    uint8_t delay_fader_mode = kDelayFaderModeSend;
    float reverb_mix = 0.0f;
    uint8_t reverb_fader_mode = kReverbFaderModeSend;
    float sat_drive  = 0.0f;
    float sat_mix    = 0.0f;
    float sat_bump   = 0.5f;
    float sat_tone   = 0.5f;   // tape mode: pre-emphasis low/high balance (0..1)
    float sat_bias   = 0.0f;   // tape mode: waveshaper asymmetry (-1..1)
    float sat_bit_reso = 0.5f;
    float sat_bit_smpl = 0.5f;
    uint8_t sat_mode = 0; // 0=tape, 1=bit
    // Global process LPF (TPT SVF) on the summed mix. cutoff in Hz; transparent
    // at max. resonance 0..1 (0 = none, ->1 = self-oscillation).
    float process_cutoff_hz = 20000.0f;
    float process_resonance = 0.0f;
    float eq_mix          = 1.0f;
    float eq_center_norm  = 0.5f;
    float eq_tilt_db      = 0.0f;
    float eq_q            = 0.6f; // peaking Q both bells, UI range 0.4..0.8
    // Tilt band sub-mode: false = tilt see-saw (low +, high -), true = a single
    // peaking bell at center_hz (same gain/center/Q controls). REnc-click toggles.
    bool  eq_tilt_is_bell = false;
    // Independent low/high shelf bands (Process->EQ). Each is a shelf at its gain,
    // or a filter (lo -> high-pass, hi -> low-pass) when *_is_filter, where the
    // gain control becomes resonance (*_q). gain ±12 dB; lo cutoff 20..500 Hz;
    // hi cutoff 2000..20000 Hz.
    float eq_lo_gain_db   = 0.0f;
    float eq_lo_cutoff_hz = 120.0f;
    bool  eq_lo_is_filter = false;
    float eq_lo_q         = 0.707f;
    float eq_hi_gain_db   = 0.0f;
    float eq_hi_cutoff_hz = 6000.0f;
    bool  eq_hi_is_filter = false;
    float eq_hi_q         = 0.707f;
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
    uint8_t engine_filter_mode[kLayerCount] = {0u, 0u}; // 0=LP, 1=HP, 2=BP
    bool  engine_loop_mode[kLayerCount] = {false, false};
    float engine_loop_attack_ms[kLayerCount] = {5.0f, 5.0f};
    float engine_loop_decay_ms[kLayerCount] = {20.0f, 20.0f};
    float engine_loop_sustain_level[kLayerCount] = {1.0f, 1.0f}; // 0=-inf, 1=0 dB
    float engine_loop_release_ms[kLayerCount] = {50.0f, 50.0f};
    uint8_t engine_loop_attack_curve[kLayerCount] = {0u, 0u};  // 0=exp, 1=log
    uint8_t engine_loop_release_curve[kLayerCount] = {0u, 0u}; // 0=exp, 1=log
    float engine_loop_crossfade_amount[kLayerCount] = {0.0625f, 0.0625f}; // 0..0.5 of selected length
    float engine_loop_crossfade_shape[kLayerCount] = {0.0f, 0.0f}; // 0=linear, 1=equal-power-like
    // ADSR playback mode (perform_adsr type, perform_adsr_row==2): a positional
    // amplitude envelope across the trimmed region. engine_adsr_mode gates it on;
    // a_x/d_x/r_x/s_level are 0..100 fractions; sustain_loop parks playback in the
    // d_x..r_x section while the key is held (else one-shot). The A/R ramp curves
    // reuse engine_loop_attack_curve / engine_loop_release_curve.
    bool    engine_adsr_mode[kLayerCount] = {false, false};
    uint8_t engine_adsr_env_a_x[kLayerCount] = {13u, 13u};
    uint8_t engine_adsr_env_d_x[kLayerCount] = {38u, 38u};
    uint8_t engine_adsr_env_r_x[kLayerCount] = {89u, 89u};
    uint8_t engine_adsr_env_s_level[kLayerCount] = {50u, 50u};
    bool    engine_adsr_sustain_loop[kLayerCount] = {false, false};
    uint8_t perform_keyzone_lo_note[kLayerCount] = {48u, 48u}; // C3
    uint8_t perform_keyzone_hi_note[kLayerCount] = {60u, 60u}; // C4
    uint8_t express_target[kLayerCount][kExpressRowCount]
        = {{kExpressNone, kExpressNone, kExpressNone},
           {kExpressNone, kExpressNone, kExpressNone}};
    uint16_t express_min_value[kLayerCount][kExpressRowCount]
        = {{0u, 0u, 0u}, {0u, 0u, 0u}};
    uint16_t express_max_value[kLayerCount][kExpressRowCount]
        = {{0u, 0u, 0u}, {0u, 0u, 0u}};
    uint8_t  express_poly_porto_voice_limit[kLayerCount]
        = {kExpressPolyPortoVoicesDefault, kExpressPolyPortoVoicesDefault};
    uint16_t express_poly_porto_slide_ms[kLayerCount]
        = {kExpressPolyPortoSlideDefaultMs, kExpressPolyPortoSlideDefaultMs};
    uint8_t  express_poly_porto_source_range_semitones[kLayerCount]
        = {kExpressPolyPortoRangeDefaultSemitones, kExpressPolyPortoRangeDefaultSemitones};
    uint8_t  express_poly_porto_source_mode[kLayerCount]
        = {kExpressPolyPortoSourceClosest, kExpressPolyPortoSourceClosest};
    uint16_t express_poly_porto_release_ms[kLayerCount]
        = {kExpressPolyPortoReleaseDefaultMs, kExpressPolyPortoReleaseDefaultMs};
    uint8_t fx_order[4] = {0, 2, 3, 1}; // ids: 0=SAT,1=EQ,2=DELAY,3=REVERB; default EQ last

    // Velocity-mod lanes (first/second). Indexed by lane (0/1). In FULL mode
    // both lanes apply to all voices; in SPLIT mode lane i applies only to
    // layer i (see perform_keyzone_is_split). target indexes kVelModTargetList
    // (0=----, 1=volume, 2=attack, 3=sustain, 4=release, 5..7=sends). amount is
    // -10..+10 (clamped per target type at the UI). threshold 0..127, shape
    // 0=knee/1=gate. Discrete config: snapped (not smoothed) into Current.
    static constexpr uint8_t kVelModLaneCount = 2;
    uint8_t velmod_target[kVelModLaneCount]    = {0u, 0u};
    int8_t  velmod_amount[kVelModLaneCount]    = {0, 0};
    uint8_t velmod_threshold[kVelModLaneCount] = {0u, 0u};
    uint8_t velmod_shape[kVelModLaneCount]     = {1u, 1u}; // default gate
    uint8_t velmod_source[kVelModLaneCount]    = {0u, 0u}; // 0=>vel 1=<vel 2=>note 3=<note
    // Keyzone SPLIT: when true, velmod lane i applies only to layer i's voices
    // (mod block A/B). When false (FULL), both lanes apply to all voices.
    bool    perform_keyzone_is_split = false;
    // Keytrack volume: tilt = -12..+12 angle (sign picks which side is cut,
    // magnitude = steepness), amount_db = 0..12 +/- swing the extremes reach,
    // mid_note = the 0 dB pivot the curve tents around. Applied per voice at
    // note-on as a gain by MIDI note across C1..C8.
    int8_t  perform_keytrack_tilt = 0;
    int8_t  perform_keytrack_amount_db = 0;
    uint8_t perform_keytrack_mid_note = 66; // F#4
};

struct PerformParamsCurrent
{
    static constexpr uint8_t kLayerCount = 2;

    float master_level = 0.70f; // OUTPUT VOL default 70 (UNITY=1.0)

    bool  delay_on  = false;
    bool  reverb_on = false;
    bool  sat_on    = false;
    bool  eq_on     = true;

    float delay_mix  = 0.0f;
    uint8_t delay_fader_mode = kDelayFaderModeSend;
    float reverb_mix = 0.0f;
    uint8_t reverb_fader_mode = kReverbFaderModeSend;
    float sat_drive  = 0.0f;
    float sat_mix    = 0.0f;
    float sat_bump   = 0.5f;
    float sat_tone   = 0.5f;   // tape mode: pre-emphasis low/high balance (0..1)
    float sat_bias   = 0.0f;   // tape mode: waveshaper asymmetry (-1..1)
    float sat_bit_reso = 0.5f;
    float sat_bit_smpl = 0.5f;
    uint8_t sat_mode = 0; // 0=tape, 1=bit
    // Global process LPF (TPT SVF) on the summed mix. cutoff in Hz; transparent
    // at max. resonance 0..1 (0 = none, ->1 = self-oscillation).
    float process_cutoff_hz = 20000.0f;
    float process_resonance = 0.0f;
    float eq_mix          = 1.0f;
    float eq_center_norm  = 0.5f;
    float eq_tilt_db      = 0.0f;
    float eq_q            = 0.6f; // peaking Q both bells, UI range 0.4..0.8
    // Tilt band sub-mode: false = tilt see-saw (low +, high -), true = a single
    // peaking bell at center_hz (same gain/center/Q controls). REnc-click toggles.
    bool  eq_tilt_is_bell = false;
    // Independent low/high shelf bands (Process->EQ). Each is a shelf at its gain,
    // or a filter (lo -> high-pass, hi -> low-pass) when *_is_filter, where the
    // gain control becomes resonance (*_q). gain ±12 dB; lo cutoff 20..500 Hz;
    // hi cutoff 2000..20000 Hz.
    float eq_lo_gain_db   = 0.0f;
    float eq_lo_cutoff_hz = 120.0f;
    bool  eq_lo_is_filter = false;
    float eq_lo_q         = 0.707f;
    float eq_hi_gain_db   = 0.0f;
    float eq_hi_cutoff_hz = 6000.0f;
    bool  eq_hi_is_filter = false;
    float eq_hi_q         = 0.707f;
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
    uint8_t engine_filter_mode[kLayerCount] = {0u, 0u}; // 0=LP, 1=HP, 2=BP
    bool  engine_loop_mode[kLayerCount] = {false, false};
    float engine_loop_attack_ms[kLayerCount] = {5.0f, 5.0f};
    float engine_loop_decay_ms[kLayerCount] = {20.0f, 20.0f};
    float engine_loop_sustain_level[kLayerCount] = {1.0f, 1.0f}; // 0=-inf, 1=0 dB
    float engine_loop_release_ms[kLayerCount] = {50.0f, 50.0f};
    uint8_t engine_loop_attack_curve[kLayerCount] = {0u, 0u};  // 0=exp, 1=log
    uint8_t engine_loop_release_curve[kLayerCount] = {0u, 0u}; // 0=exp, 1=log
    float engine_loop_crossfade_amount[kLayerCount] = {0.0625f, 0.0625f}; // 0..0.5 of selected length
    float engine_loop_crossfade_shape[kLayerCount] = {0.0f, 0.0f}; // 0=linear, 1=equal-power-like
    // ADSR playback mode (see PerformParamsTargets for field meanings). Discrete.
    bool    engine_adsr_mode[kLayerCount] = {false, false};
    uint8_t engine_adsr_env_a_x[kLayerCount] = {13u, 13u};
    uint8_t engine_adsr_env_d_x[kLayerCount] = {38u, 38u};
    uint8_t engine_adsr_env_r_x[kLayerCount] = {89u, 89u};
    uint8_t engine_adsr_env_s_level[kLayerCount] = {50u, 50u};
    bool    engine_adsr_sustain_loop[kLayerCount] = {false, false};
    uint8_t perform_keyzone_lo_note[kLayerCount] = {48u, 48u}; // C3
    uint8_t perform_keyzone_hi_note[kLayerCount] = {60u, 60u}; // C4
    uint8_t express_target[kLayerCount][kExpressRowCount]
        = {{kExpressNone, kExpressNone, kExpressNone},
           {kExpressNone, kExpressNone, kExpressNone}};
    uint16_t express_min_value[kLayerCount][kExpressRowCount]
        = {{0u, 0u, 0u}, {0u, 0u, 0u}};
    uint16_t express_max_value[kLayerCount][kExpressRowCount]
        = {{0u, 0u, 0u}, {0u, 0u, 0u}};
    uint8_t  express_poly_porto_voice_limit[kLayerCount]
        = {kExpressPolyPortoVoicesDefault, kExpressPolyPortoVoicesDefault};
    uint16_t express_poly_porto_slide_ms[kLayerCount]
        = {kExpressPolyPortoSlideDefaultMs, kExpressPolyPortoSlideDefaultMs};
    uint8_t  express_poly_porto_source_range_semitones[kLayerCount]
        = {kExpressPolyPortoRangeDefaultSemitones, kExpressPolyPortoRangeDefaultSemitones};
    uint8_t  express_poly_porto_source_mode[kLayerCount]
        = {kExpressPolyPortoSourceClosest, kExpressPolyPortoSourceClosest};
    uint16_t express_poly_porto_release_ms[kLayerCount]
        = {kExpressPolyPortoReleaseDefaultMs, kExpressPolyPortoReleaseDefaultMs};
    uint8_t fx_order[4] = {0, 2, 3, 1}; // ids: 0=SAT,1=EQ,2=DELAY,3=REVERB; default EQ last

    // Velocity-mod lanes (see PerformParamsTargets for field meanings).
    static constexpr uint8_t kVelModLaneCount = 2;
    uint8_t velmod_target[kVelModLaneCount]    = {0u, 0u};
    int8_t  velmod_amount[kVelModLaneCount]    = {0, 0};
    uint8_t velmod_threshold[kVelModLaneCount] = {0u, 0u};
    uint8_t velmod_shape[kVelModLaneCount]     = {1u, 1u}; // default gate
    uint8_t velmod_source[kVelModLaneCount]    = {0u, 0u}; // 0=>vel 1=<vel 2=>note 3=<note
    bool    perform_keyzone_is_split = false;
    int8_t  perform_keytrack_tilt = 0;       // -12..+12 (see PerformParamsTargets)
    int8_t  perform_keytrack_amount_db = 0;  // 0..12 dB +/- swing
    uint8_t perform_keytrack_mid_note = 66;  // 0 dB pivot note (F#4)
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

    // Monotonic counter bumped by PublishTargets (main loop) each time the UI
    // publishes an edit. The audio thread uses this to skip re-pushing engine
    // params into the voice engine when nothing has changed. AUDIO THREAD: read.
    uint32_t PublishGen() const { return publish_gen_.load(std::memory_order_acquire); }

    PerformParamsCurrent current;

  private:
    PerformParamsTargets targets_buf_[2];
    std::atomic<uint8_t> published_idx_{0};
    std::atomic<uint32_t> publish_gen_{0};
    uint8_t              write_idx_ = 1;

    // Cached one-pole smoothing coefficient. AudioBlockTick recomputes only
    // when block_size or sample_rate changes, avoiding a per-block std::exp.
    float  cached_smooth_coeff_ = 0.0f;
    size_t cached_block_size_   = 0;
    float  cached_sample_rate_  = 0.0f;

    // Helper: one-pole smoothing toward target
    static float SmoothToward(float current, float target, float coeff);
};
