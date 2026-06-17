#include "params.h"

#include "util/scopedirqblocker.h"
#include <cmath>

void Params::Init()
{
    const PerformParamsTargets init_t{};
    targets_buf_[0] = init_t;
    targets_buf_[1] = init_t;

    // Main writes to the buffer that is NOT published.
    published_idx_.store(0, std::memory_order_relaxed);
    write_idx_ = 1;

    current = PerformParamsCurrent{};
}

PerformParamsTargets& Params::EditTargets()
{
    return targets_buf_[write_idx_ & 1];
}

const PerformParamsTargets& Params::TargetsForUI() const
{
    const uint8_t idx = published_idx_.load(std::memory_order_acquire);
    return targets_buf_[idx & 1];
}

void Params::PublishTargets()
{
    // Protect the swap + copy from being interrupted by the audio callback.
    daisy::ScopedIrqBlocker irq;

    const uint8_t new_published = write_idx_ & 1;
    published_idx_.store(new_published, std::memory_order_release);

    // Flip to the other buffer for subsequent edits.
    write_idx_ ^= 1;

    // Seed the new write buffer from the latest published values.
    targets_buf_[write_idx_ & 1] = targets_buf_[new_published];

    // Signal the audio thread that params changed so it re-pushes them.
    publish_gen_.fetch_add(1, std::memory_order_release);
}

float Params::SmoothToward(float current_v, float target_v, float coeff)
{
    // current += (target - current) * coeff
    return current_v + (target_v - current_v) * coeff;
}

void Params::AudioBlockTick(float sample_rate, size_t block_size)
{
    if(sample_rate <= 0.0f || block_size == 0)
        return;

    constexpr float smoothing_time_sec = 0.005f; // 5ms shared smoothing time

    // Recompute the exponential one-pole coefficient only when the audio
    // block size or sample rate changes. In steady state this avoids a
    // std::exp on the audio thread every block.
    if(block_size != cached_block_size_ || sample_rate != cached_sample_rate_)
    {
        const float dt_block_sec = (float)block_size / sample_rate;
        float coeff = 1.0f - std::exp(-dt_block_sec / smoothing_time_sec);
        if(coeff < 0.0f)
            coeff = 0.0f;
        else if(coeff > 1.0f)
            coeff = 1.0f;
        cached_smooth_coeff_ = coeff;
        cached_block_size_   = block_size;
        cached_sample_rate_  = sample_rate;
    }
    const float coeff = cached_smooth_coeff_;

    const uint8_t idx = published_idx_.load(std::memory_order_acquire);
    const auto&   t   = targets_buf_[idx & 1];

    // Smooth floats
    current.master_level = SmoothToward(current.master_level, t.master_level, coeff);
    current.delay_mix    = SmoothToward(current.delay_mix, t.delay_mix, coeff);
    current.reverb_mix   = SmoothToward(current.reverb_mix, t.reverb_mix, coeff);
    current.sat_drive    = SmoothToward(current.sat_drive, t.sat_drive, coeff);
    current.sat_mix      = SmoothToward(current.sat_mix, t.sat_mix, coeff);
    current.sat_bump     = SmoothToward(current.sat_bump, t.sat_bump, coeff);
    current.sat_tone     = SmoothToward(current.sat_tone, t.sat_tone, coeff);
    current.sat_bias     = SmoothToward(current.sat_bias, t.sat_bias, coeff);
    current.sat_bit_reso = SmoothToward(current.sat_bit_reso, t.sat_bit_reso, coeff);
    current.sat_bit_smpl = SmoothToward(current.sat_bit_smpl, t.sat_bit_smpl, coeff);
    current.eq_mix         = SmoothToward(current.eq_mix, t.eq_mix, coeff);
    current.eq_center_norm = SmoothToward(current.eq_center_norm, t.eq_center_norm, coeff);
    current.eq_tilt_db     = SmoothToward(current.eq_tilt_db, t.eq_tilt_db, coeff);
    current.eq_q           = SmoothToward(current.eq_q, t.eq_q, coeff);
    current.delay_time_l   = SmoothToward(current.delay_time_l, t.delay_time_l, coeff);
    current.delay_time_r   = SmoothToward(current.delay_time_r, t.delay_time_r, coeff);
    current.delay_feedback = SmoothToward(current.delay_feedback, t.delay_feedback, coeff);
    current.reverb_pre   = SmoothToward(current.reverb_pre, t.reverb_pre, coeff);
    current.reverb_damp  = SmoothToward(current.reverb_damp, t.reverb_damp, coeff);
    current.reverb_decay = SmoothToward(current.reverb_decay, t.reverb_decay, coeff);
    current.reverb_mod   = SmoothToward(current.reverb_mod, t.reverb_mod, coeff);
    current.lpf_cutoff_hz = SmoothToward(current.lpf_cutoff_hz, t.lpf_cutoff_hz, coeff);
    current.lfo_rate_hz   = SmoothToward(current.lfo_rate_hz, t.lfo_rate_hz, coeff);
    current.lfo_depth     = SmoothToward(current.lfo_depth, t.lfo_depth, coeff);
    current.env_attack_ms = SmoothToward(current.env_attack_ms, t.env_attack_ms, coeff);
    current.env_decay_ms  = SmoothToward(current.env_decay_ms, t.env_decay_ms, coeff);
    current.env_amount    = SmoothToward(current.env_amount, t.env_amount, coeff);
    for(uint8_t layer = 0; layer < PerformParamsCurrent::kLayerCount; ++layer)
    {
        current.engine_layer_master_level[layer]
            = SmoothToward(current.engine_layer_master_level[layer],
                           t.engine_layer_master_level[layer],
                           coeff);
        current.engine_filter_cutoff_hz[layer]
            = SmoothToward(current.engine_filter_cutoff_hz[layer],
                           t.engine_filter_cutoff_hz[layer],
                           coeff);
        current.engine_filter_resonance[layer]
            = SmoothToward(current.engine_filter_resonance[layer],
                           t.engine_filter_resonance[layer],
                           coeff);
        // ENGINE tune must follow the displayed semitone/cents target exactly
        // so retriggered notes match the current UI value without slew lag.
        current.engine_tune_semitones[layer] = t.engine_tune_semitones[layer];
        current.engine_gain_db[layer]
            = SmoothToward(current.engine_gain_db[layer],
                           t.engine_gain_db[layer],
                           coeff);
        current.engine_loop_attack_ms[layer] = t.engine_loop_attack_ms[layer];
        current.engine_loop_decay_ms[layer] = t.engine_loop_decay_ms[layer];
        current.engine_loop_sustain_level[layer] = t.engine_loop_sustain_level[layer];
        current.engine_loop_release_ms[layer] = t.engine_loop_release_ms[layer];
        current.engine_loop_crossfade_amount[layer] = t.engine_loop_crossfade_amount[layer];
        current.engine_loop_crossfade_shape[layer] = t.engine_loop_crossfade_shape[layer];
    }

    // Bools snap immediately
    current.delay_on  = t.delay_on;
    current.delay_fader_mode = (t.delay_fader_mode == kDelayFaderModeMix)
                                   ? kDelayFaderModeMix
                                   : kDelayFaderModeSend;
    current.reverb_on = t.reverb_on;
    current.reverb_fader_mode = (t.reverb_fader_mode == kReverbFaderModeMix)
                                    ? kReverbFaderModeMix
                                    : kReverbFaderModeSend;
    current.sat_on    = t.sat_on;
    current.eq_on     = t.eq_on;
    current.sat_mode  = t.sat_mode;
    for(uint8_t i = 0; i < 4; ++i)
        current.fx_order[i] = t.fx_order[i];
    for(uint8_t layer = 0; layer < PerformParamsCurrent::kLayerCount; ++layer)
    {
        current.engine_loop_mode[layer] = t.engine_loop_mode[layer];
        current.engine_drive_mode[layer] = t.engine_drive_mode[layer];
        current.engine_filter_mode[layer] = t.engine_filter_mode[layer];
        current.perform_keyzone_lo_note[layer] = t.perform_keyzone_lo_note[layer];
        current.perform_keyzone_hi_note[layer] = t.perform_keyzone_hi_note[layer];
        for(uint8_t row = 0; row < kExpressRowCount; ++row)
        {
            current.express_target[layer][row] = t.express_target[layer][row];
            current.express_min_value[layer][row] = t.express_min_value[layer][row];
            current.express_max_value[layer][row] = t.express_max_value[layer][row];
        }
        current.express_poly_porto_voice_limit[layer] = t.express_poly_porto_voice_limit[layer];
        current.express_poly_porto_slide_ms[layer] = t.express_poly_porto_slide_ms[layer];
        current.express_poly_porto_source_range_semitones[layer]
            = t.express_poly_porto_source_range_semitones[layer];
        current.express_poly_porto_source_mode[layer] = t.express_poly_porto_source_mode[layer];
        current.express_poly_porto_release_ms[layer] = t.express_poly_porto_release_ms[layer];
    }

    // Velocity-mod config: discrete, snapped (read once at note-on; no slew).
    for(uint8_t lane = 0; lane < PerformParamsCurrent::kVelModLaneCount; ++lane)
    {
        current.velmod_target[lane]    = t.velmod_target[lane];
        current.velmod_amount[lane]    = t.velmod_amount[lane];
        current.velmod_threshold[lane] = t.velmod_threshold[lane];
        current.velmod_shape[lane]     = t.velmod_shape[lane];
        current.velmod_source[lane]    = t.velmod_source[lane];
    }
    current.perform_keyzone_is_split = t.perform_keyzone_is_split;
}
