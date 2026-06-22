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
static constexpr float kLoopEnvAttackMinMs        = 2.0f;
static constexpr float kLoopEnvReleaseMinMs       = 1.0f;
static constexpr float kLoopEnvAttackReleaseMaxMs = 4000.0f;

// Voice pool lives in fast RAM (DTCM). Uses `mem_regions.h` section macro.
ADSR2_SECTION(".dtcmram_bss") static Voice g_voice_pool[VoiceEngine::kMaxVoices];

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
    {
        sample_bank_[i] = bank ? bank[i] : nullptr;
        sample_edit_bank_[i] = SampleEdit_Default(0);
        sample_edit_valid_[i] = false;
    }
    for(uint8_t i = count; i < kMaxSampleBank; ++i)
    {
        sample_bank_[i] = nullptr;
        sample_edit_bank_[i] = SampleEdit_Default(0);
        sample_edit_valid_[i] = false;
    }
    BumpSetupCacheGen_(); // sample-bank changes invalidate slot/edit lookups
}

int VoiceEngine::FindSampleBankSlot_(const Sample* sample) const
{
    if(sample == nullptr)
        return -1;

    for(uint8_t i = 0; i < sample_bank_count_; ++i)
    {
        if(sample_bank_[i] == sample)
            return static_cast<int>(i);
    }
    return -1;
}

bool VoiceEngine::LookupSampleEdit_(const Sample* sample, SampleEdit& edit) const
{
    const int slot = FindSampleBankSlot_(sample);
    if(slot < 0 || !sample_edit_valid_[slot])
        return false;

    edit = sample_edit_bank_[slot];
    return true;
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

void VoiceEngine::SetVelMod(uint8_t lane,
                           uint8_t target,
                           int8_t  amount,
                           uint8_t threshold,
                           uint8_t shape,
                           uint8_t source)
{
    if(lane >= kVelModLaneCount)
        return;
    velmod_target_[lane]    = target;
    velmod_amount_[lane]    = amount;
    velmod_threshold_[lane] = threshold;
    velmod_shape_[lane]     = shape;
    velmod_source_[lane]    = (source > 3u) ? 0u : source;
    // Recompute the fast-skip flag. A lane is "active" only with a real target
    // (non-zero index = not "----") and non-zero amount.
    bool any = false;
    for(uint8_t i = 0; i < kVelModLaneCount; ++i)
    {
        if(velmod_target_[i] != 0u && velmod_amount_[i] != 0)
        {
            any = true;
            break;
        }
    }
    velmod_any_active_ = any;
}

// Per-lane source. Domain selects which value drives the gate + knee (velocity
// or MIDI note); polarity selects whether the lane triggers at/above (>) or
// at/below (<) the threshold. Note thresholds are constrained to C1..C8 at the
// UI; the knee ramps over the distance toward the far end of that range.
static constexpr uint8_t kVelModSourceGtVel  = 0u;
static constexpr uint8_t kVelModSourceLtVel  = 1u;
static constexpr uint8_t kVelModSourceGtNote = 2u;
static constexpr uint8_t kVelModSourceLtNote = 3u;
static constexpr int kVelModNoteLo = 24; // C1
static constexpr int kVelModNoteHi = 108; // C8

float VoiceEngine::VelModFractionForTarget_(uint8_t target_code,
                                            uint8_t velocity,
                                            uint8_t note,
                                            uint8_t layer) const
{
    // Both lanes (A/B) apply to every note and gate purely on their own source +
    // threshold + shape — there is no mode flag. Two lanes may target the same
    // param, so contributions ACCUMULATE across lanes (a lane gated out by its
    // own threshold simply contributes nothing). A clean keyboard split is just
    // lane A = <note and lane B = >note at the same threshold; in the overlap
    // (or for vel sources) both lanes add. (Single layer: no source_layer route.)
    (void)layer;
    float acc = 0.0f;
    for(uint8_t lane = 0; lane < kVelModLaneCount; ++lane)
    {
        if(velmod_target_[lane] != target_code)
            continue;
        if(velmod_amount_[lane] == 0)
            continue;

        const uint8_t source = velmod_source_[lane];
        const bool note_domain = (source == kVelModSourceGtNote) || (source == kVelModSourceLtNote);
        const bool above       = (source == kVelModSourceGtVel)  || (source == kVelModSourceGtNote);
        const int  src         = note_domain ? static_cast<int>(note) : static_cast<int>(velocity);
        const int  thr         = static_cast<int>(velmod_threshold_[lane]);
        // Knee ramps from 0 at the threshold to 1 at the far end of the domain in
        // the active direction. Above: threshold -> hi end; below: threshold -> lo end.
        const int hi = note_domain ? kVelModNoteHi : 127;
        const int lo = note_domain ? kVelModNoteLo : 0;

        if(above)
        {
            if(src < thr)
                continue; // this lane gated out below threshold
        }
        else if(src > thr)
            continue; // this lane gated out above threshold

        float scale;
        if(velmod_shape_[lane] == 1u)
        {
            scale = 1.0f; // gate: full amount once on the active side of threshold
        }
        else
        {
            // knee: linear ramp over the distance from threshold toward the
            // domain's far end (key-tracking for note source). Sends get a power
            // curve so harder hits scale disproportionately; volume/ADSR stay linear.
            const int span = above ? (hi - thr) : (thr - lo);
            const int dist = above ? (src - thr) : (thr - src);
            float lin = (span <= 0) ? 1.0f
                                    : (static_cast<float>(dist) / static_cast<float>(span));
            if(lin < 0.0f) lin = 0.0f;
            if(lin > 1.0f) lin = 1.0f;
            if(target_code >= 5u)
            {
                constexpr float kVelModSendKneeCurve = 1.5f; // tuning knob
                scale = std::pow(lin, kVelModSendKneeCurve);
            }
            else
            {
                scale = lin;
            }
        }
        acc += (static_cast<float>(velmod_amount_[lane]) * 0.1f) * scale;
    }
    // Clamp the summed contribution so two lanes sharing a target can't drive a
    // param past its single-lane range: sends stay within kSendMaxGain (+6 dB),
    // modifiers (volume/attack/sustain/release) within their documented ±range.
    if(target_code >= 5u)
    {
        if(acc < 0.0f) acc = 0.0f;
        if(acc > 1.0f) acc = 1.0f;
    }
    else
    {
        if(acc < -1.0f) acc = -1.0f;
        if(acc > 1.0f) acc = 1.0f;
    }
    return acc;
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

void VoiceEngine::SetLoopEnvelopeCurves(uint8_t layer, bool attack_log, bool release_log)
{
    layer &= 1u;
    loop_env_attack_log_[layer] = attack_log;
    loop_env_release_log_[layer] = release_log;
}

void VoiceEngine::SetLoopCrossfadeAmount(uint8_t layer, float amount)
{
    layer &= 1u;
    if(amount < 0.0f)
        amount = 0.0f;
    if(amount > 0.5f)
        amount = 0.5f;
    if(loop_crossfade_amount_[layer] != amount)
    {
        loop_crossfade_amount_[layer] = amount;
        BumpSetupCacheGen_(); // affects cached seam_frames
    }
}

void VoiceEngine::SetLoopCrossfadeShape(uint8_t layer, float shape)
{
    layer &= 1u;
    if(shape < 0.0f)
        shape = 0.0f;
    if(shape > 1.0f)
        shape = 1.0f;
    // Shape is not in the setup cache (it's used in the per-sample seam blend,
    // not setup), so no gen bump needed.
    loop_crossfade_shape_[layer] = shape;
}

void VoiceEngine::SetLayerSeamBaked(uint8_t layer, bool baked)
{
    layer &= 1u;
    // Pushed every block by the audio thread; bump only on actual change so the
    // setup cache isn't invalidated each block.
    if(layer_seam_baked_[layer] != baked)
    {
        layer_seam_baked_[layer] = baked;
        BumpSetupCacheGen_(); // affects cached crossfade_seam_frames
    }
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

    for(uint8_t i = 0; i < kMaxSampleBank; ++i)
    {
        sample_bank_[i] = nullptr;
        sample_edit_bank_[i] = SampleEdit_Default(0);
        sample_edit_valid_[i] = false;
    }

    for(size_t i = 0; i < kMaxVoices; i++)
    {
        voices_[i] = Voice{};
        voices_[i].mod_env.Init(sample_rate_);
        voices_[i].release_coeff = block_release_coeff_;
    }
    // Engine reset: invalidate all per-voice setup caches. (Each entry's
    // gen_seen no longer matches setup_cache_gen_; first-pass setup recomputes.)
    BumpSetupCacheGen_();
    for(size_t i = 0; i < kMaxVoices; i++)
        voice_setup_cache_[i] = VoiceBlockSetupCache{};

    for(uint8_t layer = 0; layer < kEngineLayerCount; ++layer)
    {
        engine_layer_scale_[layer] = 1.0f;
        engine_loop_enabled_[layer] = false;
        loop_env_attack_ms_[layer] = 5.0f;
        loop_env_decay_ms_[layer] = 20.0f;
        loop_env_sustain_level_[layer] = 1.0f;
        loop_env_release_ms_[layer] = 50.0f;
        loop_crossfade_amount_[layer] = 0.0625f;
        loop_crossfade_shape_[layer] = 0.0f;
        layer_seam_baked_[layer] = false;
        poly_porto_enabled_[layer] = false;
        poly_porto_voice_limit_[layer] = kExpressPolyPortoVoicesDefault;
        poly_porto_slide_ms_[layer] = static_cast<float>(kExpressPolyPortoSlideDefaultMs);
        poly_porto_source_range_semitones_[layer] = kExpressPolyPortoRangeDefaultSemitones;
        poly_porto_source_mode_[layer] = kExpressPolyPortoSourceClosest;
        poly_porto_release_ms_[layer] = static_cast<float>(kExpressPolyPortoReleaseDefaultMs);
    }
    poly_porto_source_order_counter_ = 0;
    audio_sample_counter_ = 0;

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
