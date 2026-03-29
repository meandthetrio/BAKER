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
    const float     dt_block_sec       = (float)block_size / sample_rate;

    // Exponential (RC-style) one-pole:
    // coeff = 1 - exp(-dt / tau)
    float coeff = 1.0f - std::exp(-dt_block_sec / smoothing_time_sec);
    if(coeff < 0.0f)
        coeff = 0.0f;
    else if(coeff > 1.0f)
        coeff = 1.0f;

    const uint8_t idx = published_idx_.load(std::memory_order_acquire);
    const auto&   t   = targets_buf_[idx & 1];

    // Smooth floats
    current.master_level = SmoothToward(current.master_level, t.master_level, coeff);
    current.delay_mix    = SmoothToward(current.delay_mix, t.delay_mix, coeff);
    current.reverb_mix   = SmoothToward(current.reverb_mix, t.reverb_mix, coeff);
    current.sat_drive    = SmoothToward(current.sat_drive, t.sat_drive, coeff);
    current.sat_mix      = SmoothToward(current.sat_mix, t.sat_mix, coeff);
    current.sat_bump     = SmoothToward(current.sat_bump, t.sat_bump, coeff);
    current.sat_bit_reso = SmoothToward(current.sat_bit_reso, t.sat_bit_reso, coeff);
    current.sat_bit_smpl = SmoothToward(current.sat_bit_smpl, t.sat_bit_smpl, coeff);
    current.mod_mix      = SmoothToward(current.mod_mix, t.mod_mix, coeff);
    current.mod_rate_hz  = SmoothToward(current.mod_rate_hz, t.mod_rate_hz, coeff);
    current.mod_wow      = SmoothToward(current.mod_wow, t.mod_wow, coeff);
    current.tape_rate    = SmoothToward(current.tape_rate, t.tape_rate, coeff);
    current.delay_time   = SmoothToward(current.delay_time, t.delay_time, coeff);
    current.delay_feedback = SmoothToward(current.delay_feedback, t.delay_feedback, coeff);
    current.delay_spread = SmoothToward(current.delay_spread, t.delay_spread, coeff);
    current.delay_freeze = SmoothToward(current.delay_freeze, t.delay_freeze, coeff);
    current.reverb_pre   = SmoothToward(current.reverb_pre, t.reverb_pre, coeff);
    current.reverb_damp  = SmoothToward(current.reverb_damp, t.reverb_damp, coeff);
    current.reverb_decay = SmoothToward(current.reverb_decay, t.reverb_decay, coeff);
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
        current.engine_tune_semitones[layer]
            = SmoothToward(current.engine_tune_semitones[layer],
                           t.engine_tune_semitones[layer],
                           coeff);
        current.engine_gain_db[layer]
            = SmoothToward(current.engine_gain_db[layer],
                           t.engine_gain_db[layer],
                           coeff);
        current.engine_loop_attack_ms[layer] = t.engine_loop_attack_ms[layer];
        current.engine_loop_decay_ms[layer] = t.engine_loop_decay_ms[layer];
        current.engine_loop_sustain_level[layer] = t.engine_loop_sustain_level[layer];
        current.engine_loop_release_ms[layer] = t.engine_loop_release_ms[layer];
        current.engine_loop_crossfade_amount[layer] = t.engine_loop_crossfade_amount[layer];
    }

    // Bools snap immediately
    current.delay_on  = t.delay_on;
    current.reverb_on = t.reverb_on;
    current.sat_on    = t.sat_on;
    current.mod_on    = t.mod_on;
    current.sat_mode  = t.sat_mode;
    current.mod_mode  = t.mod_mode;
    current.reverb_reverse = t.reverb_reverse;
    for(uint8_t i = 0; i < 4; ++i)
        current.fx_order[i] = t.fx_order[i];
    for(uint8_t layer = 0; layer < PerformParamsCurrent::kLayerCount; ++layer)
    {
        current.engine_loop_mode[layer] = t.engine_loop_mode[layer];
        current.perform_keyzone_lo_note[layer] = t.perform_keyzone_lo_note[layer];
        current.perform_keyzone_hi_note[layer] = t.perform_keyzone_hi_note[layer];
    }
}
