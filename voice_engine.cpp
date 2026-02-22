#include "voice_engine.h"

#include "build_config.h"
#include "mem_regions.h"
#include "macros.h"

#include <cmath>
#include <cstring>

static constexpr float kReleaseTauSec      = 0.030f;
static constexpr float kReleaseAmpEpsilon  = 1e-4f;
static constexpr float kVoiceAmpScale      = 0.15f; // keep headroom for 10 voices + FX
static constexpr float kStealXfadeSec      = 0.001f;
static constexpr float kFadeInMs           = 3.0f;
static constexpr float kStopFadeMs         = 3.0f;
static constexpr float kStopFadeMinMs      = 1.0f;
static constexpr float kStopFadeMaxMs      = 5.0f;
static constexpr float kTwoPi              = 6.2831853071795864769f;
static constexpr float kEnvAttackSec       = 0.005f;
static constexpr float kEnvDecaySec        = 0.060f;
static constexpr float kEnvSustainLevel    = 0.70f;
static constexpr float kEnvReleaseSec      = 0.030f;
static constexpr float kMinRatio           = 0.25f;
static constexpr float kMaxRatio           = 4.0f;
static constexpr float kPitchModSemitones  = 2.0f;
static constexpr float kLockCutoffMinHz    = 80.0f;
static constexpr float kLockCutoffMaxHz    = 12000.0f;
static constexpr float kMacroSmoothSec     = 0.005f;

// Voice pool lives in fast RAM (DTCM). Uses `mem_regions.h` section macro.
ADSR2_SECTION(".dtcmram") static Voice g_voice_pool[VoiceEngine::kMaxVoices];

static inline float ComputeRatio(uint8_t note, uint8_t root_key)
{
    const float semitones = static_cast<float>((int)note - (int)root_key);
    float ratio = std::pow(2.0f, semitones / 12.0f);
    if(ratio < kMinRatio)
        ratio = kMinRatio;
    if(ratio > kMaxRatio)
        ratio = kMaxRatio;
    return ratio;
}

static inline float ComputeFadeStep(float sample_rate)
{
    int fade_samples = static_cast<int>(sample_rate * 0.001f * kFadeInMs);
    if(fade_samples < 1)
        fade_samples = 1;
    return 1.0f / static_cast<float>(fade_samples);
}

static inline void InitEnvelope(EnvStage& stage,
                                float&    level,
                                float&    a_step,
                                float&    d_step,
                                float&    r_step,
                                float&    sustain,
                                float     sample_rate)
{
    int a_samps = static_cast<int>(kEnvAttackSec * sample_rate);
    int d_samps = static_cast<int>(kEnvDecaySec * sample_rate);
    int r_samps = static_cast<int>(kEnvReleaseSec * sample_rate);
    if(a_samps < 1) a_samps = 1;
    if(d_samps < 1) d_samps = 1;
    if(r_samps < 1) r_samps = 1;

    sustain = kEnvSustainLevel;
    stage   = EnvStage::Attack;
    level   = 0.0f;
    a_step  = 1.0f / static_cast<float>(a_samps);
    d_step  = (1.0f - sustain) / static_cast<float>(d_samps);
    r_step  = sustain / static_cast<float>(r_samps);
    if(r_step < 1e-6f)
        r_step = 1e-6f;
}

static inline void SetEnvelopeRelease(EnvStage& stage,
                                      float&    level,
                                      float&    r_step,
                                      float     sample_rate)
{
    int r_samps = static_cast<int>(kEnvReleaseSec * sample_rate);
    if(r_samps < 1) r_samps = 1;
    stage = EnvStage::Release;
    r_step = level / static_cast<float>(r_samps);
    if(r_step < 1e-6f)
        r_step = 1e-6f;
}

static inline void StepEnvelope(EnvStage& stage,
                                float&    level,
                                float     a_step,
                                float     d_step,
                                float     sustain,
                                float     r_step)
{
    switch(stage)
    {
        case EnvStage::Attack:
            level += a_step;
            if(level >= 1.0f)
            {
                level = 1.0f;
                stage = EnvStage::Decay;
            }
            break;
        case EnvStage::Decay:
            level -= d_step;
            if(level <= sustain)
            {
                level = sustain;
                stage = EnvStage::Sustain;
            }
            break;
        case EnvStage::Sustain:
            level = sustain;
            break;
        case EnvStage::Release:
            level -= r_step;
            if(level <= 0.0f)
            {
                level = 0.0f;
                stage = EnvStage::Off;
            }
            break;
        case EnvStage::Off:
        default:
            level = 0.0f;
            break;
    }
}

static inline bool AdvancePos(float& pos,
                              int8_t& dir,
                              float ratio,
                              float len,
                              float ls,
                              float le,
                              bool loop_enabled,
                              bool gate,
                              LoopMode mode)
{
    if(!loop_enabled || !gate || le <= ls || le > len)
    {
        pos += ratio;
        if(pos >= len)
            return false;
        return true;
    }

    if(mode == LoopMode::Forward)
    {
        pos += ratio;
        if(pos >= le)
            pos = ls + (pos - le);
    }
    else
    {
        pos += ratio * static_cast<float>(dir);
        if(dir > 0 && pos >= le)
        {
            pos = le - (pos - le);
            dir = -1;
        }
        else if(dir < 0 && pos <= ls)
        {
            pos = ls + (ls - pos);
            dir = 1;
        }
    }
    return true;
}

static inline float SampleAtLinear(const Sample* s, float pos, bool wrap_end)
{
    if(s == nullptr || s->pcm == nullptr || s->length == 0)
        return 0.0f;
    const uint32_t i = static_cast<uint32_t>(pos);
    if(i >= s->length)
        return 0.0f;
    const float frac = pos - static_cast<float>(i);
    const int16_t a = s->pcm[i];
    const int16_t b = (i + 1 < s->length) ? s->pcm[i + 1] : (wrap_end ? s->pcm[0] : a);
    const float fa = static_cast<float>(a) * (1.0f / 32768.0f);
    const float fb = static_cast<float>(b) * (1.0f / 32768.0f);
    return fa + frac * (fb - fa);
}

static inline float SampleAtLinearRegion(const Sample* s,
                                         float pos,
                                         uint32_t start,
                                         uint32_t end,
                                         bool loop_enabled,
                                         uint32_t loop_start,
                                         uint32_t loop_end)
{
    if(s == nullptr || s->pcm == nullptr || s->length == 0)
        return 0.0f;
    if(end <= start || end > s->length)
        end = s->length;
    if(pos < (float)start || pos >= (float)end)
        return 0.0f;

    const uint32_t i = static_cast<uint32_t>(pos);
    if(i < start || i >= end)
        return 0.0f;
    const float frac = pos - static_cast<float>(i);
    const int16_t a = s->pcm[i];
    uint32_t next = i + 1;
    if(next >= end)
    {
        if(loop_enabled && loop_start < loop_end)
            next = loop_start;
        else
            next = i;
    }
    const int16_t b = s->pcm[next];
    const float fa = static_cast<float>(a) * (1.0f / 32768.0f);
    const float fb = static_cast<float>(b) * (1.0f / 32768.0f);
    return fa + frac * (fb - fa);
}

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
                            std::atomic<uint32_t>* lfo_depth_dbg)
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

void VoiceEngine::StartStopFade_(Voice& v)
{
    if(v.stop_fade_active)
        return;

    int32_t fade_samples = stop_fade_samples_;
    if(fade_samples < 1)
        fade_samples = 1;

    v.stop_fade_active = true;
    v.stop_fade_samples_remaining = fade_samples;
    v.stop_fade_level = 1.0f;
    v.stop_fade_step = 1.0f / static_cast<float>(fade_samples);
    v.gate = false;
    v.new_gate = false;
    v.old_gate = false;

    if(fadeouts_started_)
        fadeouts_started_->fetch_add(1, std::memory_order_relaxed);
}

void VoiceEngine::FinishStopFade_(Voice& v)
{
    v.state = VoiceState::Idle;
    v.sample = nullptr;
    v.pos = 0.0f;
    v.ratio = 1.0f;
    v.gain = 0.0f;
    v.vel_layer = 0;
    v.vel_brightness = 1.0f;
    v.fade_in = 0.0f;
    v.fade_in_step = 0.0f;
    v.env_stage = EnvStage::Off;
    v.env_level = 0.0f;
    v.gate = false;
    v.dir  = 1;
    v.new_pos = 0.0f;
    v.new_ratio = 1.0f;
    v.new_gain = 0.0f;
    v.new_fade_in = 0.0f;
    v.new_fade_in_step = 0.0f;
    v.new_env_stage = EnvStage::Off;
    v.new_env_level = 0.0f;
    v.new_gate = false;
    v.old_gate = false;
    v.mod_env.Reset();
    v.stop_fade_active = false;
    v.stop_fade_samples_remaining = 0;
    v.stop_fade_level = 0.0f;
    v.stop_fade_step = 0.0f;
}

uint32_t VoiceEngine::PackVoiceDebug_(uint8_t idx, uint8_t note, uint8_t vel)
{
    return (uint32_t)idx | ((uint32_t)note << 8) | ((uint32_t)vel << 16);
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

void VoiceEngine::StartVoice_(Voice& v,
                              const Sample* sample,
                              uint8_t note,
                              uint8_t velocity,
                              uint8_t vel_layer,
                              uint32_t start_id)
{
    if(sample == nullptr || sample->pcm == nullptr || sample->length == 0)
    {
        v.state    = VoiceState::Idle;
        v.note     = note;
        v.velocity = velocity;
        v.vel_layer = vel_layer;
        v.start_id = start_id;
        return;
    }

    v.state    = VoiceState::Playing;
    v.note     = note;
    v.velocity = velocity;
    v.vel_layer = vel_layer;
    v.start_id = start_id;

    v.sample = sample;
    float start_pos = 0.0f;
    if(edit_sample_ == sample)
    {
        SampleEdit e = current_edit_;
        SampleEdit_Clamp(e, sample->length);
        start_pos = static_cast<float>(e.start_frame);
    }
    v.pos    = start_pos;
    v.ratio  = ComputeRatio(note, sample->root_key);
    const float vel01 = (velocity > 127) ? 1.0f : ((float)velocity / 127.0f);
    v.gain = vel01 * kVoiceAmpScale;
    v.vel_brightness = (vel_layer == 0) ? 0.35f : 1.0f;
    v.lpf_z = 0.0f;
    v.gate = true;
    v.dir  = 1;
    v.fade_in = 0.0f;
    v.fade_in_step = ComputeFadeStep(sample_rate_);
    v.stop_fade_active = false;
    v.stop_fade_samples_remaining = 0;
    v.stop_fade_level = 0.0f;
    v.stop_fade_step = 0.0f;
    InitEnvelope(v.env_stage,
                 v.env_level,
                 v.env_a_step,
                 v.env_d_step,
                 v.env_r_step,
                 v.env_sustain,
                 sample_rate_);

    v.release_coeff = block_release_coeff_;
}

int VoiceEngine::AllocateVoice_(bool& stole,
                                uint8_t& stolen_index,
                                uint32_t& stolen_start_id)
{
    stole = false;
    stolen_index = 0;
    stolen_start_id = 0;

    // Prefer the first idle voice.
    for(size_t i = 0; i < kMaxVoices; i++)
    {
        if(voices_[i].state == VoiceState::Idle)
            return (int)i;
    }

    // No free voices: steal Oldest Note (smallest `start_id`).
    size_t   best_idx = 0;
    uint32_t best_start_id = 0xFFFFFFFFu;
    for(size_t i = 0; i < kMaxVoices; i++)
    {
        if(voices_[i].state != VoiceState::Idle
           && voices_[i].start_id < best_start_id)
        {
            best_start_id = voices_[i].start_id;
            best_idx = i;
        }
    }

    if(voice_steals_)
        voice_steals_->fetch_add(1, std::memory_order_relaxed);
    steals_total_++;

    stole = true;
    stolen_index    = static_cast<uint8_t>(best_idx);
    stolen_start_id = best_start_id;

    return (int)best_idx;
}

void VoiceEngine::AllNotesOff_()
{
    for(size_t i = 0; i < kMaxVoices; i++)
    {
        Voice& v = voices_[i];
        if(v.state == VoiceState::Idle)
            continue;
        StartStopFade_(v);
    }
}

void VoiceEngine::NoteOff_(uint8_t note)
{
    for(size_t i = 0; i < kMaxVoices; i++)
    {
        Voice& v = voices_[i];
        if(v.note != note)
            continue;
        if(v.state == VoiceState::Playing || v.state == VoiceState::Releasing)
        {
            StartStopFade_(v);
            v.state = VoiceState::Releasing;
            SetEnvelopeRelease(v.env_stage, v.env_level, v.env_r_step, sample_rate_);
            v.gate = false;
            v.dir  = 1;
        }
        else if(v.state == VoiceState::StealXFade)
        {
            StartStopFade_(v);
            v.pos   = v.new_pos;
            v.gain  = v.new_gain;
            v.ratio = v.new_ratio;
            v.fade_in = v.new_fade_in;
            v.fade_in_step = v.new_fade_in_step;
            SetEnvelopeRelease(v.new_env_stage, v.new_env_level, v.new_env_r_step, sample_rate_);
            v.new_gate = false;
            v.new_dir  = 1;
        }
    }
}

void VoiceEngine::ProcessEvents(EventQueueSPSC& q)
{
    Event e;
    while(q.Pop(e))
    {
        if(events_popped_)
            events_popped_->fetch_add(1, std::memory_order_relaxed);

        switch(e.type)
        {
            case EventType::TestPing: break;
            case EventType::NoteOn:
            {
                const uint8_t note = e.note;
                const uint8_t vel  = e.velocity;
                const uint8_t sample_index = static_cast<uint8_t>(e.value & 0xFFu);
                uint8_t vel_layer = static_cast<uint8_t>((e.value >> 8) & 0xFFu);
                if(vel_layer > 1)
                    vel_layer = 1;
                const float vel_brightness = (vel_layer == 0) ? 0.35f : 1.0f;
                const Sample* sample = nullptr;
                if(sample_index < sample_bank_count_)
                    sample = sample_bank_[sample_index];
                if(sample == nullptr)
                    sample = current_sample_;
                if(sample == nullptr || sample->pcm == nullptr || sample->length == 0)
                    continue;

                bool     stole = false;
                uint8_t  stolen_index = 0;
                uint32_t stolen_start_id = 0;
                const int idx = AllocateVoice_(stole, stolen_index, stolen_start_id);
                if(idx >= 0)
                {
                    const uint32_t start_id = ++note_start_counter_;
                    Voice& v = voices_[(size_t)idx];
                    if(stole)
                    {
                        const float vel01 = (vel > 127) ? 1.0f : ((float)vel / 127.0f);

                        v.sample  = sample;
                        v.vel_layer = vel_layer;
                        v.vel_brightness = vel_brightness;
                        v.mod_env.Trigger(env_attack_ms_, env_decay_ms_);
                        v.stop_fade_active = false;
                        v.stop_fade_samples_remaining = 0;
                        v.stop_fade_level = 0.0f;
                        v.stop_fade_step = 0.0f;
                        v.old_pos = v.pos;
                        v.old_ratio = v.ratio;
                        const float old_fin = (v.fade_in < 1.0f) ? v.fade_in : 1.0f;
                        const float old_env = (v.env_level < 1.0f) ? v.env_level : 1.0f;
                        v.old_gain = v.gain * old_fin * old_env;
                        v.old_gate = v.gate;
                        v.old_dir  = v.dir;

                        v.new_pos = 0.0f;
                        if(edit_sample_ == sample)
                        {
                            SampleEdit e = current_edit_;
                            SampleEdit_Clamp(e, sample->length);
                            v.new_pos = static_cast<float>(e.start_frame);
                        }
                        v.new_ratio = ComputeRatio(note, sample->root_key);
                        v.new_gain = vel01 * kVoiceAmpScale;
                        v.new_fade_in = 0.0f;
                        v.new_fade_in_step = ComputeFadeStep(sample_rate_);
                        v.new_gate = true;
                        v.new_dir  = 1;
                        InitEnvelope(v.new_env_stage,
                                     v.new_env_level,
                                     v.new_env_a_step,
                                     v.new_env_d_step,
                                     v.new_env_r_step,
                                     v.new_env_sustain,
                                     sample_rate_);

                        int xfade_samples = (int)(sample_rate_ * kStealXfadeSec);
                        if(xfade_samples < 16)
                            xfade_samples = 16;
                        if(xfade_samples > 128)
                            xfade_samples = 128;

                        v.xfade_pos  = 0.0f;
                        v.xfade_step = 1.0f / (float)xfade_samples;

                        v.state    = VoiceState::StealXFade;
                        v.note     = note;
                        v.velocity = vel;
                        v.start_id = start_id;
                    }
                    else
                    {
                        StartVoice_(v, sample, note, vel, vel_layer, start_id);
                        v.mod_env.Trigger(env_attack_ms_, env_decay_ms_);
                    }

                    if(stole)
                    {
                        last_stolen_voice_index_ = stolen_index;
                        last_stolen_start_id_    = stolen_start_id;
                        last_new_start_id_       = start_id;
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

                    if(last_voice_packed_)
                        last_voice_packed_->store(PackVoiceDebug_((uint8_t)idx, note, vel),
                                                  std::memory_order_relaxed);
                }
            }
            break;
            case EventType::NoteOff:
                NoteOff_(e.note);
                break;
            case EventType::AllNotesOff:
                AllNotesOff_();
                break;
        }
    }
}

void VoiceEngine::RenderBlock(float* outL, float* outR, size_t size)
{
    if(!outL || !outR || size == 0)
        return;

    const LoopMode loop_mode = GetLoopMode();
    if(macro_gen_ && macro_sel_ && macro_a_ && macro_b_)
    {
        const uint32_t gen = macro_gen_->load(std::memory_order_acquire);
        if(gen != macro_gen_seen_)
        {
            const uint8_t sel = macro_sel_->load(std::memory_order_acquire) & 1u;
            active_macros_ = (sel == 0) ? *macro_a_ : *macro_b_;
            macro_gen_seen_ = gen;
        }
    }
    if(plocks_)
    {
        const uint32_t gen = plocks_->lock_gen.load(std::memory_order_acquire);
        if(gen != lock_gen_seen_)
        {
            const uint8_t sel = plocks_->lock_sel.load(std::memory_order_acquire) & 1u;
            active_lock_ = (sel == 0) ? plocks_->lock_a : plocks_->lock_b;
            lock_gen_seen_ = gen;
        }
    }

    const SampleEdit edit = current_edit_;
    const Sample* edit_sample = edit_sample_;

    float cutoff_norm = 0.0f;
    if(active_lock_.enabled)
    {
        cutoff_norm = active_lock_.cutoff_norm;
    }
    else
    {
        float hz = lpf_cutoff_hz_;
        if(hz < kLockCutoffMinHz) hz = kLockCutoffMinHz;
        if(hz > kLockCutoffMaxHz) hz = kLockCutoffMaxHz;
        const float ratio = kLockCutoffMaxHz / kLockCutoffMinHz;
        cutoff_norm = std::log(hz / kLockCutoffMinHz) / std::log(ratio);
    }
    if(cutoff_norm < 0.0f) cutoff_norm = 0.0f;
    if(cutoff_norm > 1.0f) cutoff_norm = 1.0f;

    float lfo_depth = lfo_depth_;
    float env_amount = env_amount_;

    float rate_hz = lfo_rate_hz_;
    float depth   = lfo_depth;
#if LFO_SWEEP_TEST
    const float dt = static_cast<float>(size) / sample_rate_;
    const float phase_inc = dt / 10.0f;
    sweep_phase_rate_ += sweep_dir_rate_ * phase_inc;
    if(sweep_phase_rate_ >= 1.0f)
    {
        sweep_phase_rate_ = 1.0f;
        sweep_dir_rate_ = -1.0f;
    }
    else if(sweep_phase_rate_ <= 0.0f)
    {
        sweep_phase_rate_ = 0.0f;
        sweep_dir_rate_ = 1.0f;
    }

    sweep_phase_depth_ += sweep_dir_depth_ * phase_inc;
    if(sweep_phase_depth_ >= 1.0f)
    {
        sweep_phase_depth_ = 1.0f;
        sweep_dir_depth_ = -1.0f;
    }
    else if(sweep_phase_depth_ <= 0.0f)
    {
        sweep_phase_depth_ = 0.0f;
        sweep_dir_depth_ = 1.0f;
    }

    rate_hz = 0.2f + (8.0f - 0.2f) * sweep_phase_rate_;
    depth   = 0.1f + (0.9f - 0.1f) * sweep_phase_depth_;
    if(rate_hz < 0.2f)
        rate_hz = 0.2f;
    if(rate_hz > 8.0f)
        rate_hz = 8.0f;
    if(depth < 0.1f)
        depth = 0.1f;
    if(depth > 0.9f)
        depth = 0.9f;
#endif

    const ModRoute* routes = nullptr;
    ModRoute routes_local[kMaxModRoutes];
    if(mod_matrix_)
    {
        const uint8_t sel = mod_matrix_->routes_sel.load(std::memory_order_acquire) & 1u;
        routes = (sel == 0) ? mod_matrix_->routes_a : mod_matrix_->routes_b;
        for(size_t ri = 0; ri < kMaxModRoutes; ++ri)
            routes_local[ri] = routes[ri];
    }

    float route0_amount = (routes) ? routes_local[0].amount : 0.0f;
    Macros_Smooth(macro_smoothed_, active_macros_, macro_smooth_coeff_);
    Macros_Apply(macro_smoothed_, &cutoff_norm, &lfo_depth, &env_amount, &route0_amount, nullptr);

    if(routes)
    {
        routes_local[0].amount = route0_amount;
        routes = routes_local;
    }

    const float ratio = kLockCutoffMaxHz / kLockCutoffMinHz;
    float cutoff_hz = kLockCutoffMinHz * std::pow(ratio, cutoff_norm);
    if(cutoff_hz < 20.0f)
        cutoff_hz = 20.0f;
    if(cutoff_hz > 20000.0f)
        cutoff_hz = 20000.0f;

    depth = lfo_depth;
    lfo_.SetRateHz(rate_hz);
    lfo_.SetWave(lfo_wave_);
    const float lfo_val = lfo_.Value();
    lfo_.TickBlock(size);
    const float lfo_src = lfo_val * depth;

    if(size != block_size_)
    {
        block_size_ = size;
        const float dt_block = (float)block_size_ / sample_rate_;
        block_release_coeff_ = std::exp(-dt_block / kReleaseTauSec);
        macro_smooth_coeff_  = 1.0f - std::exp(-dt_block / kMacroSmoothSec);
        if(macro_smooth_coeff_ < 0.0f)
            macro_smooth_coeff_ = 0.0f;
        if(macro_smooth_coeff_ > 1.0f)
            macro_smooth_coeff_ = 1.0f;
        for(size_t i = 0; i < kMaxVoices; i++)
            voices_[i].release_coeff = block_release_coeff_;
    }

    std::memset(outL, 0, sizeof(float) * size);
    std::memset(outR, 0, sizeof(float) * size);

    uint32_t active = 0;
    uint32_t clip_block = 0;
    float max_env = 0.0f;
    const float mix_scale = 0.7f / static_cast<float>(VoiceEngine::kMaxVoices);

    for(size_t vi = 0; vi < kMaxVoices; vi++)
    {
        Voice& v = voices_[vi];
        if(v.state == VoiceState::Idle)
            continue;

        bool     stop_fade_active = v.stop_fade_active;
        int32_t  stop_fade_remaining = v.stop_fade_samples_remaining;
        float    stop_fade_level = v.stop_fade_level;
        float    stop_fade_step = v.stop_fade_step;

        active++;

        const float env_val = v.mod_env.Value() * env_amount;
        v.mod_env.TickBlock(size);
        if(env_val > max_env)
            max_env = env_val;

        float mod_cutoff = 0.0f;
        float mod_pitch  = 0.0f;
        if(routes)
        {
            for(size_t ri = 0; ri < kMaxModRoutes; ++ri)
            {
                const ModRoute& r = routes[ri];
                if(r.enabled == 0)
                    continue;
                float src_val = 0.0f;
                if(r.src == static_cast<uint8_t>(ModSource::LFO))
                    src_val = lfo_src;
                else if(r.src == static_cast<uint8_t>(ModSource::ModEnv))
                    src_val = env_val;

                const float mod_val = src_val * r.amount;
                if(r.dst == static_cast<uint8_t>(ModDest::FilterCutoff))
                    mod_cutoff += mod_val;
                else if(r.dst == static_cast<uint8_t>(ModDest::Pitch))
                    mod_pitch += mod_val;
            }
        }

        if(mod_pitch > 1.0f)
            mod_pitch = 1.0f;
        if(mod_pitch < -1.0f)
            mod_pitch = -1.0f;
        const float pitch_scale = std::pow(2.0f, (mod_pitch * kPitchModSemitones) / 12.0f);

        if(mod_cutoff > 1.0f)
            mod_cutoff = 1.0f;
        if(mod_cutoff < -0.95f)
            mod_cutoff = -0.95f;

        float voice_cutoff = cutoff_hz * v.vel_brightness;
        voice_cutoff = voice_cutoff * (1.0f + mod_cutoff);
        if(voice_cutoff < 20.0f)
            voice_cutoff = 20.0f;
        if(voice_cutoff > 20000.0f)
            voice_cutoff = 20000.0f;
        float lpf_g = 1.0f - std::exp(-kTwoPi * voice_cutoff / sample_rate_);
        if(lpf_g < 0.001f)
            lpf_g = 0.001f;
        else if(lpf_g > 0.999f)
            lpf_g = 0.999f;

        if(v.state == VoiceState::StealXFade)
        {
            if(v.sample == nullptr || v.sample->pcm == nullptr || v.sample->length == 0)
            {
                v.state = VoiceState::Idle;
                continue;
            }

            float old_pos = v.old_pos;
            float new_pos = v.new_pos;
            bool old_gate = v.old_gate;
            bool new_gate = v.new_gate;
            int8_t old_dir = v.old_dir;
            int8_t new_dir = v.new_dir;
            bool use_edit = (edit_sample != nullptr && v.sample == edit_sample);
            SampleEdit e = edit;
            uint32_t start = 0;
            uint32_t end = v.sample->length;
            uint32_t ls_i = v.sample->loop_start;
            uint32_t le_i = v.sample->loop_end;
            bool loop_enabled = v.sample->loop_enabled;
            float edit_gain = 1.0f;
            if(use_edit)
            {
                SampleEdit_Clamp(e, v.sample->length);
                start = e.start_frame;
                end = e.end_frame;
                ls_i = e.loop_start;
                le_i = e.loop_end;
                loop_enabled = (e.loop_enable != 0);
                edit_gain = e.gain;
            }
            const float length_f = static_cast<float>(end);
            const float ls = static_cast<float>(ls_i);
            const float le = static_cast<float>(le_i);
            if(loop_enabled && old_pos >= length_f)
                old_pos = ls + (old_pos - length_f);
            if(loop_enabled && new_pos >= length_f)
                new_pos = ls + (new_pos - length_f);
            if(old_pos < static_cast<float>(start))
                old_pos = static_cast<float>(start);
            if(new_pos < static_cast<float>(start))
                new_pos = static_cast<float>(start);
            if(!loop_enabled && old_pos >= length_f && length_f > 0.0f)
            {
                old_gate = false;
                old_pos = length_f - 1.0f;
            }
            if(!loop_enabled && new_pos >= length_f && length_f > 0.0f)
            {
                new_gate = false;
                new_pos = length_f - 1.0f;
            }

            const float old_ratio = v.old_ratio * pitch_scale;
            const float new_ratio = v.new_ratio * pitch_scale;
            const float old_gain = v.old_gain * edit_gain;
            const float new_gain = v.new_gain * edit_gain;
            float x = v.xfade_pos;
            const float x_step = v.xfade_step;
            float new_fade = v.new_fade_in;
            const float new_fade_step = v.new_fade_in_step;
            EnvStage new_env_stage = v.new_env_stage;
            float new_env_level = v.new_env_level;
            const float new_env_a_step = v.new_env_a_step;
            const float new_env_d_step = v.new_env_d_step;
            float new_env_r_step = v.new_env_r_step;
            const float new_env_sustain = v.new_env_sustain;
            float lpf_z = v.lpf_z;

            if(stop_fade_active)
            {
                old_gate = false;
                new_gate = false;
            }

            for(size_t i = 0; i < size; i++)
            {
                float x_clamped = x;
                if(x_clamped > 1.0f)
                    x_clamped = 1.0f;

                float s_old = 0.0f;
                float s_new = 0.0f;
                if(use_edit)
                {
                    s_old = SampleAtLinearRegion(v.sample,
                                                 old_pos,
                                                 start,
                                                 end,
                                                 loop_enabled,
                                                 ls_i,
                                                 le_i) * old_gain;
                    s_new = SampleAtLinearRegion(v.sample,
                                                 new_pos,
                                                 start,
                                                 end,
                                                 loop_enabled,
                                                 ls_i,
                                                 le_i) * new_gain;
                }
                else
                {
                    const bool old_wrap = (loop_enabled && old_gate
                                           && v.sample->loop_start == 0
                                           && v.sample->loop_end == v.sample->length);
                    const bool new_wrap = (loop_enabled && new_gate
                                           && v.sample->loop_start == 0
                                           && v.sample->loop_end == v.sample->length);
                    s_old = SampleAtLinear(v.sample, old_pos, old_wrap) * old_gain;
                    s_new = SampleAtLinear(v.sample, new_pos, new_wrap) * new_gain;
                }
                s_new *= new_env_level;
                const float fin_new = (new_fade < 1.0f) ? new_fade : 1.0f;
                s_new *= fin_new;

                float s     = s_old * (1.0f - x_clamped) + s_new * x_clamped;
                if(stop_fade_active)
                    s *= stop_fade_level;
                lpf_z += lpf_g * (s - lpf_z);
                outL[i] += lpf_z;
                outR[i] += lpf_z;

                if(stop_fade_active)
                {
                    stop_fade_remaining--;
                    stop_fade_level -= stop_fade_step;
                    if(stop_fade_level < 0.0f)
                        stop_fade_level = 0.0f;
                    if(stop_fade_remaining <= 0)
                    {
                        FinishStopFade_(v);
                        break;
                    }
                }

                if(!AdvancePos(old_pos,
                               old_dir,
                               old_ratio,
                               length_f,
                               ls,
                               le,
                               loop_enabled,
                               old_gate,
                               loop_mode))
                {
                    old_gate = false;
                }
                if(!AdvancePos(new_pos,
                               new_dir,
                               new_ratio,
                               length_f,
                               ls,
                               le,
                               loop_enabled,
                               new_gate,
                               loop_mode))
                {
                    new_env_stage = EnvStage::Off;
                    new_env_level = 0.0f;
                    new_gate = false;
                    if(!stop_fade_active)
                    {
                        StartStopFade_(v);
                        stop_fade_active = v.stop_fade_active;
                        stop_fade_remaining = v.stop_fade_samples_remaining;
                        stop_fade_level = v.stop_fade_level;
                        stop_fade_step = v.stop_fade_step;
                        old_gate = false;
                        new_gate = false;
                    }
                }

                if(!old_gate && old_pos >= length_f && length_f > 0.0f)
                    old_pos = length_f - 1.0f;
                if(!new_gate && new_pos >= length_f && length_f > 0.0f)
                    new_pos = length_f - 1.0f;

                x += x_step;
                if(new_fade < 1.0f)
                {
                    new_fade += new_fade_step;
                    if(new_fade > 1.0f)
                        new_fade = 1.0f;
                }

                StepEnvelope(new_env_stage,
                             new_env_level,
                             new_env_a_step,
                             new_env_d_step,
                             new_env_sustain,
                             new_env_r_step);
                if(new_env_stage == EnvStage::Off)
                    new_env_level = 0.0f;
            }

            if(v.state == VoiceState::Idle)
                continue;

            v.old_pos   = old_pos;
            v.new_pos   = new_pos;
            v.xfade_pos  = x;
            v.new_fade_in = new_fade;
            v.new_env_stage = new_env_stage;
            v.new_env_level = new_env_level;
            v.new_env_r_step = new_env_r_step;
            v.old_gate = old_gate;
            v.old_dir  = old_dir;
            v.new_gate = new_gate;
            v.new_dir  = new_dir;
            v.lpf_z    = lpf_z;
            v.stop_fade_active = stop_fade_active;
            v.stop_fade_samples_remaining = stop_fade_remaining;
            v.stop_fade_level = stop_fade_level;
            v.stop_fade_step = stop_fade_step;

            if(v.xfade_pos >= 1.0f)
            {
                v.pos  = v.new_pos;
                v.gain = v.new_gain;
                v.ratio = v.new_ratio;
                v.fade_in = v.new_fade_in;
                v.fade_in_step = v.new_fade_in_step;
                v.env_stage = v.new_env_stage;
                v.env_level = v.new_env_level;
                v.env_a_step = v.new_env_a_step;
                v.env_d_step = v.new_env_d_step;
                v.env_r_step = v.new_env_r_step;
                v.env_sustain = v.new_env_sustain;
                v.gate = v.new_gate;
                v.dir  = v.new_dir;
                if(v.env_stage == EnvStage::Off)
                    v.state = VoiceState::Idle;
                else if(v.env_stage == EnvStage::Release)
                    v.state = VoiceState::Releasing;
                else
                    v.state = VoiceState::Playing;
            }
        }
        else
        {
            if(v.sample == nullptr || v.sample->pcm == nullptr || v.sample->length == 0)
            {
                v.state = VoiceState::Idle;
                continue;
            }

            float pos = v.pos;
            const float ratio = v.ratio * pitch_scale;
            bool use_edit = (edit_sample != nullptr && v.sample == edit_sample);
            SampleEdit e = edit;
            uint32_t start = 0;
            uint32_t end = v.sample->length;
            uint32_t ls_i = v.sample->loop_start;
            uint32_t le_i = v.sample->loop_end;
            bool loop_enabled = v.sample->loop_enabled;
            float edit_gain = 1.0f;
            if(use_edit)
            {
                SampleEdit_Clamp(e, v.sample->length);
                start = e.start_frame;
                end = e.end_frame;
                ls_i = e.loop_start;
                le_i = e.loop_end;
                loop_enabled = (e.loop_enable != 0);
                edit_gain = e.gain;
            }
            const float gain  = v.gain * edit_gain;
            const float length_f = static_cast<float>(end);
            const float ls = static_cast<float>(ls_i);
            const float le = static_cast<float>(le_i);
            bool gate = v.gate;
            int8_t dir = v.dir;
            if(stop_fade_active)
                gate = false;
            if(loop_enabled && gate && pos >= length_f)
                pos = ls + (pos - length_f);
            if(pos < static_cast<float>(start))
                pos = static_cast<float>(start);
            float fade = v.fade_in;
            const float fade_step = v.fade_in_step;
            EnvStage env_stage = v.env_stage;
            float env_level = v.env_level;
            const float env_a_step = v.env_a_step;
            const float env_d_step = v.env_d_step;
            float env_r_step = v.env_r_step;
            const float env_sustain = v.env_sustain;
            float lpf_z = v.lpf_z;

            for(size_t i = 0; i < size; i++)
            {
                float s = 0.0f;
                if(use_edit)
                {
                    s = SampleAtLinearRegion(v.sample,
                                             pos,
                                             start,
                                             end,
                                             loop_enabled,
                                             ls_i,
                                             le_i) * gain;
                }
                else
                {
                    const bool wrap_end = (loop_enabled && gate
                                           && v.sample->loop_start == 0
                                           && v.sample->loop_end == v.sample->length);
                    s = SampleAtLinear(v.sample, pos, wrap_end) * gain;
                }
                s *= env_level;
                const float fin = (fade < 1.0f) ? fade : 1.0f;
                s *= fin;
                if(stop_fade_active)
                    s *= stop_fade_level;
                lpf_z += lpf_g * (s - lpf_z);
                outL[i] += lpf_z;
                outR[i] += lpf_z;

                if(stop_fade_active)
                {
                    stop_fade_remaining--;
                    stop_fade_level -= stop_fade_step;
                    if(stop_fade_level < 0.0f)
                        stop_fade_level = 0.0f;
                    if(stop_fade_remaining <= 0)
                    {
                        FinishStopFade_(v);
                        break;
                    }
                }

                if(!AdvancePos(pos,
                               dir,
                               ratio,
                               length_f,
                               ls,
                               le,
                               loop_enabled,
                               gate,
                               loop_mode))
                {
                    if(!stop_fade_active)
                    {
                        StartStopFade_(v);
                        stop_fade_active = v.stop_fade_active;
                        stop_fade_remaining = v.stop_fade_samples_remaining;
                        stop_fade_level = v.stop_fade_level;
                        stop_fade_step = v.stop_fade_step;
                        gate = false;
                    }
                    if(length_f > 0.0f)
                        pos = length_f - 1.0f;
                }
                if(fade < 1.0f)
                {
                    fade += fade_step;
                    if(fade > 1.0f)
                        fade = 1.0f;
                }

                StepEnvelope(env_stage,
                             env_level,
                             env_a_step,
                             env_d_step,
                             env_sustain,
                             env_r_step);
                if(env_stage == EnvStage::Off && !stop_fade_active)
                {
                    v.state = VoiceState::Idle;
                    break;
                }
            }

            if(v.state != VoiceState::Idle)
            {
                v.pos = pos;
                v.fade_in = fade;
                v.env_stage = env_stage;
                v.env_level = env_level;
                v.env_r_step = env_r_step;
                v.gate = gate;
                v.dir  = dir;
                v.lpf_z = lpf_z;
                v.stop_fade_active = stop_fade_active;
                v.stop_fade_samples_remaining = stop_fade_remaining;
                v.stop_fade_level = stop_fade_level;
                v.stop_fade_step = stop_fade_step;
            }
        }
    }

    for(size_t i = 0; i < size; i++)
    {
        float mix = outL[i] * mix_scale;
        outL[i] = mix;
        outR[i] = mix;
        if(mix > 1.0f || mix < -1.0f)
            clip_block++;
    }

    if(clip_count_ && clip_block > 0)
        clip_count_->fetch_add(clip_block, std::memory_order_relaxed);

    if(lfo_rate_dbg_out_)
        lfo_rate_dbg_out_->store((uint32_t)(rate_hz * 10.0f + 0.5f),
                                 std::memory_order_relaxed);
    if(lfo_depth_dbg_out_)
        lfo_depth_dbg_out_->store((uint32_t)(depth * 100.0f + 0.5f),
                                  std::memory_order_relaxed);

    if(last_lfo_out_)
        last_lfo_out_->store((int32_t)(lfo_src * 1000.0f), std::memory_order_relaxed);
    if(last_env_out_)
        last_env_out_->store((int32_t)(max_env * 1000.0f), std::memory_order_relaxed);

    if(voices_active_)
        voices_active_->store(active, std::memory_order_relaxed);
    active_last_block_ = active;
}
