#include "voice_engine_internal.h"
#include "voice_engine_render_internal.h"

#include "build_config.h"
#include "mem_regions.h"
#include "macros.h"

#include <cmath>

static constexpr float kReleaseTauSec      = 0.030f;
static constexpr float kReleaseAmpEpsilon  = 1e-4f;
static constexpr float kStopFadeMs         = 3.0f;
static constexpr float kStopFadeMinMs      = 1.0f;
static constexpr float kStopFadeMaxMs      = 5.0f;
static constexpr float kLockCutoffMinHz    = 80.0f;
static constexpr float kLockCutoffMaxHz    = 12000.0f;
static constexpr float kMacroSmoothSec     = 0.005f;
static constexpr float kEngineTuneMinSemitones = -48.0f;
static constexpr float kEngineTuneMaxSemitones = 48.0f;
static constexpr float kEngineGainMinDb = -48.0f;
static constexpr float kEngineGainMaxDb = 12.0f;
static constexpr float kLoopEnvAttackMinMs        = 2.0f;
static constexpr float kLoopEnvReleaseMinMs       = 1.0f;
static constexpr float kLoopEnvAttackReleaseMaxMs = 1000.0f;

// Voice pool lives in fast RAM (DTCM). Uses `mem_regions.h` section macro.
ADSR2_SECTION(".dtcmram") static Voice g_voice_pool[VoiceEngine::kMaxVoices];

void VoiceEngine::BindDebug(std::atomic<uint32_t>* events_popped,
                            std::atomic<uint32_t>* voices_active,
                            std::atomic<uint32_t>* voice_steals,
                            std::atomic<uint32_t>* last_voice_packed,
                            std::atomic<uint32_t>* last_stolen_voice_index,
                            std::atomic<uint32_t>* last_stolen_start_id,
                            std::atomic<uint32_t>* last_new_start_id,
                            std::atomic<uint32_t>* clip_count,
                            std::atomic<uint32_t>* fadeouts_started,
                            std::atomic<int32_t>* last_lfo,
                            std::atomic<int32_t>* last_env,
                            std::atomic<uint32_t>* lfo_rate_dbg,
                            std::atomic<uint32_t>* lfo_depth_dbg,
                            std::atomic<uint32_t>* playhead_frame_a,
                            std::atomic<uint32_t>* playhead_frame_b,
                            std::atomic<uint32_t>* playhead_active_a,
                            std::atomic<uint32_t>* playhead_active_b)
{
    events_popped_     = events_popped;
    voices_active_     = voices_active;
    voice_steals_      = voice_steals;
    last_voice_packed_ = last_voice_packed;
    last_stolen_voice_index_out_ = last_stolen_voice_index;
    last_stolen_start_id_out_    = last_stolen_start_id;
    last_new_start_id_out_       = last_new_start_id;
    clip_count_        = clip_count;
    fadeouts_started_  = fadeouts_started;
    last_lfo_out_      = last_lfo;
    last_env_out_      = last_env;
    lfo_rate_dbg_out_  = lfo_rate_dbg;
    lfo_depth_dbg_out_ = lfo_depth_dbg;
    playhead_frame_out_[0] = playhead_frame_a;
    playhead_frame_out_[1] = playhead_frame_b;
    playhead_active_out_[0] = playhead_active_a;
    playhead_active_out_[1] = playhead_active_b;
}

void VoiceEngine::SetSampleBank(const Sample* const* bank, uint8_t count)
{
    if(count > kMaxSampleBank)
        count = kMaxSampleBank;
    sample_bank_count_ = count;
    for(uint8_t i = 0; i < count; ++i)
        sample_bank_[i] = bank ? bank[i] : nullptr;
    for(uint8_t i = count; i < kMaxSampleBank; ++i)
        sample_bank_[i] = nullptr;
}

void VoiceEngine::SetModParams(float lfo_rate_hz,
                               float lfo_depth,
                               float env_attack_ms,
                               float env_decay_ms,
                               float env_amount)
{
    if(lfo_rate_hz < 0.0f)
        lfo_rate_hz = 0.0f;
    if(lfo_depth < 0.0f)
        lfo_depth = 0.0f;
    if(lfo_depth > 1.0f)
        lfo_depth = 1.0f;
    if(env_attack_ms < 0.0f)
        env_attack_ms = 0.0f;
    if(env_attack_ms < kLoopEnvAttackMinMs)
        env_attack_ms = kLoopEnvAttackMinMs;
    if(env_decay_ms < 0.0f)
        env_decay_ms = 0.0f;
    if(env_amount < 0.0f)
        env_amount = 0.0f;
    if(env_amount > 1.0f)
        env_amount = 1.0f;

    lfo_rate_hz_   = lfo_rate_hz;
    lfo_depth_     = lfo_depth;
    env_attack_ms_ = env_attack_ms;
    env_decay_ms_  = env_decay_ms;
    env_amount_    = env_amount;
}

void VoiceEngine::SetLfoWave(uint8_t wave)
{
    lfo_wave_ = (wave == 0) ? 0 : 1;
}

void VoiceEngine::SetEngineTuneSemitones(uint8_t layer, float semitones)
{
    layer &= 1u;
    if(semitones < kEngineTuneMinSemitones)
        semitones = kEngineTuneMinSemitones;
    if(semitones > kEngineTuneMaxSemitones)
        semitones = kEngineTuneMaxSemitones;
    if(engine_tune_semitones_[layer] != semitones)
    {
        engine_tune_semitones_[layer] = semitones;
        engine_tune_dirty_[layer]     = true;
    }
}

void VoiceEngine::SetEngineGainDb(uint8_t layer, float db)
{
    layer &= 1u;
    db *= 0.1f; // UI/publish path stores tenths of dB.
    if(db < kEngineGainMinDb)
        db = kEngineGainMinDb;
    if(db > kEngineGainMaxDb)
        db = kEngineGainMaxDb;
    engine_gain_linear_[layer] = std::pow(10.0f, db / 20.0f);
    emphasis_dirty_[layer] = true;
}

void VoiceEngine::SetEngineDriveMode(uint8_t layer, uint8_t mode)
{
    layer &= 1u;
    engine_drive_mode_[layer] = (mode == 0u) ? 0u : 1u;
    emphasis_dirty_[layer] = true;
}

void VoiceEngine::SetEngineLayerScale(uint8_t layer, float scale)
{
    layer &= 1u;
    if(scale < 0.0f)
        scale = 0.0f;
    // Keep headroom bounded but allow SETTINGS-like boost parity.
    if(scale > 14.0f)
        scale = 14.0f;
    engine_layer_scale_[layer] = scale;
}

void VoiceEngine::SetEngineFilterCutoffHz(uint8_t layer, float hz)
{
    layer &= 1u;
    if(hz < 20.0f)
        hz = 20.0f;
    if(hz > 20000.0f)
        hz = 20000.0f;
    engine_filter_cutoff_hz_[layer] = hz;
    emphasis_dirty_[layer] = true;
}

void VoiceEngine::SetEngineFilterResonance(uint8_t layer, float resonance)
{
    layer &= 1u;
    if(resonance < 0.0f)
        resonance = 0.0f;
    if(resonance > 1.0f)
        resonance = 1.0f;
    engine_filter_resonance_[layer] = resonance;
    emphasis_dirty_[layer] = true;
}

void VoiceEngine::SetEngineLoopEnabled(uint8_t layer, bool enabled)
{
    engine_loop_enabled_[layer & 1u] = enabled;
}

void VoiceEngine::SetLoopEnvelopeParams(uint8_t layer,
                                        float attack_ms,
                                        float decay_ms,
                                        float sustain_level,
                                        float release_ms)
{
    layer &= 1u;
    if(attack_ms < kLoopEnvAttackMinMs)
        attack_ms = kLoopEnvAttackMinMs;
    if(attack_ms > kLoopEnvAttackReleaseMaxMs)
        attack_ms = kLoopEnvAttackReleaseMaxMs;
    if(decay_ms < 0.0f)
        decay_ms = 0.0f;
    if(sustain_level < 0.0f)
        sustain_level = 0.0f;
    if(sustain_level > 1.0f)
        sustain_level = 1.0f;
    if(release_ms < kLoopEnvReleaseMinMs)
        release_ms = kLoopEnvReleaseMinMs;
    if(release_ms > kLoopEnvAttackReleaseMaxMs)
        release_ms = kLoopEnvAttackReleaseMaxMs;

    loop_env_attack_ms_[layer] = attack_ms;
    loop_env_decay_ms_[layer] = decay_ms;
    loop_env_sustain_level_[layer] = sustain_level;
    loop_env_release_ms_[layer] = release_ms;
}

void VoiceEngine::SetLoopCrossfadeAmount(uint8_t layer, float amount)
{
    layer &= 1u;
    if(amount < 0.0f)
        amount = 0.0f;
    if(amount > 0.5f)
        amount = 0.5f;
    loop_crossfade_amount_[layer] = amount;
}

void VoiceEngine::SetLoopCrossfadeShape(uint8_t layer, float shape)
{
    layer &= 1u;
    if(shape < 0.0f)
        shape = 0.0f;
    if(shape > 1.0f)
        shape = 1.0f;
    loop_crossfade_shape_[layer] = shape;
}

void VoiceEngine::Init(float sample_rate, size_t block_size)
{
    voices_      = g_voice_pool;
    sample_rate_ = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    block_size_  = (block_size > 0) ? block_size : 48;

    const float dt_block = (float)block_size_ / sample_rate_;
    block_release_coeff_ = std::exp(-dt_block / kReleaseTauSec);
    macro_smooth_coeff_  = 1.0f - std::exp(-dt_block / kMacroSmoothSec);
    if(macro_smooth_coeff_ < 0.0f)
        macro_smooth_coeff_ = 0.0f;
    if(macro_smooth_coeff_ > 1.0f)
        macro_smooth_coeff_ = 1.0f;
    const float fade_ms = kStopFadeMs;
    const float min_ms = kStopFadeMinMs;
    const float max_ms = kStopFadeMaxMs;
    int32_t fade_samples = static_cast<int32_t>(sample_rate_ * 0.001f * fade_ms + 0.5f);
    const int32_t min_samples = static_cast<int32_t>(sample_rate_ * 0.001f * min_ms + 0.5f);
    const int32_t max_samples = static_cast<int32_t>(sample_rate_ * 0.001f * max_ms + 0.5f);
    if(fade_samples < min_samples)
        fade_samples = min_samples;
    if(fade_samples > max_samples)
        fade_samples = max_samples;
    if(fade_samples < 1)
        fade_samples = 1;
    stop_fade_samples_ = fade_samples;

    VoiceRenderFetch_InitSqrtLut();

    // Precompute pitch mod LUT: mod_pitch [-1,1] -> pow(2, mod_pitch / 12) [±1 semitone]
    for(int i = 0; i < 256; ++i)
    {
        const float t = (static_cast<float>(i) / 255.0f) * 2.0f - 1.0f;
        pitch_mod_lut_[i] = std::pow(2.0f, t / 12.0f);
    }

    lfo_.Init(sample_rate_);
    sweep_phase_rate_  = 0.0f;
    sweep_dir_rate_    = 1.0f;
    sweep_phase_depth_ = 0.0f;
    sweep_dir_depth_   = 1.0f;
    lock_gen_seen_     = 0;
    active_lock_.enabled = 0;
    active_lock_.cutoff_norm = 0.0f;
    macro_gen_seen_ = 0;
    Macros_InitState(active_macros_);
    Macros_InitState(macro_smoothed_);

    note_start_counter_ = 0;
    active_last_block_ = 0;
    steals_total_      = 0;
    last_stolen_voice_index_ = 0;
    last_stolen_start_id_    = 0;
    last_new_start_id_       = 0;
    sample_bank_count_       = 0;
    current_sample_          = nullptr;
    edit_sample_             = nullptr;
    current_edit_            = SampleEdit_Default(0);

    for(uint8_t i = 0; i < kMaxSampleBank; ++i)
        sample_bank_[i] = nullptr;

    for(size_t i = 0; i < kMaxVoices; i++)
    {
        voices_[i] = Voice{};
        voices_[i].mod_env.Init(sample_rate_);
        voices_[i].release_coeff = block_release_coeff_;
    }

    for(uint8_t layer = 0; layer < kEngineLayerCount; ++layer)
    {
        engine_gain_linear_[layer] = 1.0f;
        engine_drive_mode_[layer] = 0u;
        layer_bus_state_[layer] = LayerBusState{};
        engine_layer_scale_[layer] = 1.0f;
        engine_filter_cutoff_hz_[layer] = 20000.0f;
        engine_filter_resonance_[layer] = 0.0f;
        engine_loop_enabled_[layer] = false;
        loop_env_attack_ms_[layer] = 5.0f;
        loop_env_decay_ms_[layer] = 20.0f;
        loop_env_sustain_level_[layer] = 1.0f;
        loop_env_release_ms_[layer] = 50.0f;
        loop_crossfade_amount_[layer] = 0.0625f;
        loop_crossfade_shape_[layer] = 0.0f;
    }

    RecomputeLayerEmphasisCoeffs_(0u);
    RecomputeLayerEmphasisCoeffs_(1u);

    if(voices_active_)
        voices_active_->store(0, std::memory_order_relaxed);
    if(last_stolen_voice_index_out_)
        last_stolen_voice_index_out_->store(last_stolen_voice_index_,
                                            std::memory_order_relaxed);
    if(last_stolen_start_id_out_)
        last_stolen_start_id_out_->store(last_stolen_start_id_,
                                         std::memory_order_relaxed);
    if(last_new_start_id_out_)
        last_new_start_id_out_->store(last_new_start_id_,
                                      std::memory_order_relaxed);
}
