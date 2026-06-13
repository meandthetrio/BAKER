#include "voice_engine_internal.h"

static constexpr float kVoiceAmpScale      = 0.15f; // keep headroom for 10 voices + FX
static constexpr float kDefaultEnvAttackMs = 5.0f;
static constexpr float kDefaultEnvDecayMs  = 60.0f;
static constexpr float kDefaultEnvSustainLevel = 0.70f;
static constexpr float kDefaultEnvReleaseMs = 30.0f;

static inline uint8_t VoiceLayerForAllocation(const Voice& v)
{
    if(v.state == VoiceState::StealFadeOut)
        return v.new_source_layer & 1u;
    return v.source_layer & 1u;
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
    v.pos_frame = 0u;
    v.pos_frac = 0.0f;
    v.ratio = 1.0f;
    v.gain = 0.0f;
    v.lpf_z = 0.0f;
    v.lpf_bp = 0.0f;
    v.source_layer = 0;
    v.vel_layer = 0;
    v.vel_brightness = 1.0f;
    v.fade_in = 0.0f;
    v.fade_in_step = 0.0f;
    v.env_stage = EnvStage::Off;
    v.env_level = 0.0f;
    v.gate = false;
    v.loop_voice = false;
    v.dir  = 1;
    v.new_pos_frame = 0u;
    v.new_pos_frac = 0.0f;
    v.new_ratio = 1.0f;
    v.new_gain = 0.0f;
    v.old_source_layer = 0;
    v.new_source_layer = 0;
    v.new_env_stage = EnvStage::Off;
    v.new_env_level = 0.0f;
    v.new_gate = false;
    v.new_loop_voice = false;
    v.old_gate = false;
    v.mod_env.Reset();
    ClearPolyPortoVoice_(v);
    v.stop_fade_active = false;
    v.stop_fade_samples_remaining = 0;
    v.stop_fade_level = 0.0f;
    v.stop_fade_step = 0.0f;
}

uint32_t VoiceEngine::PackVoiceDebug_(uint8_t idx, uint8_t note, uint8_t vel)
{
    return (uint32_t)idx | ((uint32_t)note << 8) | ((uint32_t)vel << 16);
}

void VoiceEngine::StartVoice_(Voice& v,
                              const Sample* sample,
                              uint8_t note,
                              uint8_t velocity,
                              uint8_t source_layer,
                              uint8_t vel_layer,
                              uint32_t start_id)
{
    if(sample == nullptr || sample->pcm == nullptr || sample->length == 0)
    {
        v.state    = VoiceState::Idle;
        v.note     = note;
        v.midi_note = note;
        v.velocity = velocity;
        v.source_layer = source_layer & 1u;
        v.vel_layer = vel_layer;
        v.loop_voice = false;
        v.start_id = start_id;
        return;
    }

    v.state    = VoiceState::Playing;
    v.note     = note;
    v.midi_note = note;
    v.velocity = velocity;
    v.source_layer = source_layer & 1u;
    v.vel_layer = vel_layer;
    v.start_id = start_id;

    v.sample = sample;
    uint32_t start_frame = 0u;
    SampleEdit e{};
    if(LookupSampleEdit_(sample, e))
    {
        SampleEdit_Clamp(e, sample->length);
        start_frame = e.start_frame;
    }
    v.pos_frame = start_frame;
    v.pos_frac  = 0.0f;
    v.ratio  = ComputeRatio(note, sample->root_key);
    const float vel01 = (velocity > 127) ? 1.0f : ((float)velocity / 127.0f);
    v.gain = vel01 * kVoiceAmpScale;
    v.vel_brightness = (vel_layer == 0) ? 0.35f : 1.0f;
    v.lpf_z = 0.0f;
    v.lpf_bp = 0.0f;
    v.gate = true;
    v.loop_voice = engine_loop_enabled_[v.source_layer];
    v.dir  = 1;
    // Amplitude ramp is ADSR attack only (InitEnvelope); no separate fade_in.
    v.fade_in      = 1.0f;
    v.fade_in_step = 0.0f;
    v.stop_fade_active = false;
    v.stop_fade_samples_remaining = 0;
    v.stop_fade_level = 0.0f;
    v.stop_fade_step = 0.0f;
    v.new_loop_voice = false;
    InitEnvelope(v.env_stage,
                 v.env_level,
                 v.env_a_step,
                 v.env_d_step,
                 v.env_r_step,
                 v.env_sustain,
                 v.loop_voice ? loop_env_attack_ms_[v.source_layer] : kDefaultEnvAttackMs,
                 v.loop_voice ? loop_env_decay_ms_[v.source_layer] : kDefaultEnvDecayMs,
                 v.loop_voice ? loop_env_sustain_level_[v.source_layer]
                              : kDefaultEnvSustainLevel,
                 v.loop_voice ? loop_env_release_ms_[v.source_layer] : kDefaultEnvReleaseMs,
                 sample_rate_);

    v.release_coeff = block_release_coeff_;
    const float semitones = static_cast<float>((int)note - (int)sample->root_key);
    v.poly_porto_current_semitones = semitones;
    v.poly_porto_target_semitones = semitones;
    v.poly_porto_step_semitones = 0.0f;
    v.poly_porto_slide_samples_remaining = 0u;
    v.poly_porto_glide_active = false;
    v.poly_porto_managed = false;
    MarkPolyPortoHeldSource_(v);
}

int VoiceEngine::AllocateVoice_(uint8_t source_layer,
                                bool& stole,
                                uint8_t& stolen_index,
                                uint32_t& stolen_start_id)
{
    return AllocateVoiceExcluding_(source_layer, -1, stole, stolen_index, stolen_start_id);
}

int VoiceEngine::AllocateVoiceExcluding_(uint8_t source_layer,
                                         int exclude_index,
                                         bool& stole,
                                         uint8_t& stolen_index,
                                         uint32_t& stolen_start_id)
{
    stole = false;
    stolen_index = 0;
    stolen_start_id = 0;
    source_layer &= 1u;

    uint8_t layer_active = 0;
    for(size_t i = 0; i < kMaxVoices; i++)
    {
        if(static_cast<int>(i) == exclude_index)
            continue;
        if(voices_[i].state != VoiceState::Idle
           && VoiceLayerForAllocation(voices_[i]) == source_layer)
            ++layer_active;
    }

    if(layer_active < kMaxVoicesPerLayer)
    {
        for(size_t i = 0; i < kMaxVoices; i++)
        {
            if(static_cast<int>(i) == exclude_index)
                continue;
            if(voices_[i].state == VoiceState::Idle)
                return static_cast<int>(i);
        }
    }

    size_t   best_idx = 0;
    uint32_t best_start_id = 0xFFFFFFFFu;

    // Steal victim policy (same layer, same polyphony cap): avoid interrupting
    // StealFadeOut when other voices exist on the layer; prefer releasing notes
    // (already dying); else oldest start_id among eligible voices (avoids
    // stealing low-env attack notes that "quietest" would wrongly prefer).
    bool non_xf_on_layer = false;
    for(size_t i = 0; i < kMaxVoices; i++)
    {
        if(voices_[i].state == VoiceState::Idle)
            continue;
        if(VoiceLayerForAllocation(voices_[i]) != source_layer)
            continue;
        if(voices_[i].state != VoiceState::StealFadeOut)
        {
            non_xf_on_layer = true;
            break;
        }
    }

    auto victim_ok = [&](size_t i) -> bool {
        if(static_cast<int>(i) == exclude_index)
            return false;
        if(voices_[i].state == VoiceState::Idle)
            return false;
        if(VoiceLayerForAllocation(voices_[i]) != source_layer)
            return false;
        if(voices_[i].state == VoiceState::StealFadeOut && non_xf_on_layer)
            return false;
        return true;
    };

    for(size_t i = 0; i < kMaxVoices; i++)
    {
        if(!victim_ok(i))
            continue;
        if(voices_[i].state != VoiceState::Releasing)
            continue;
        if(voices_[i].start_id < best_start_id)
        {
            best_start_id = voices_[i].start_id;
            best_idx      = i;
        }
    }

    if(best_start_id == 0xFFFFFFFFu)
    {
        for(size_t i = 0; i < kMaxVoices; i++)
        {
            if(!victim_ok(i))
                continue;
            if(voices_[i].start_id < best_start_id)
            {
                best_start_id = voices_[i].start_id;
                best_idx      = i;
            }
        }
    }

    if(best_start_id == 0xFFFFFFFFu)
    {
        for(size_t i = 0; i < kMaxVoices; i++)
        {
            if(static_cast<int>(i) == exclude_index)
                continue;
            if(voices_[i].state != VoiceState::Idle
               && voices_[i].start_id < best_start_id)
            {
                best_start_id = voices_[i].start_id;
                best_idx = i;
            }
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
        {
            ClearPolyPortoVoice_(v);
            continue;
        }
        StartStopFade_(v);
        ClearPolyPortoVoice_(v);
    }
}

void VoiceEngine::NoteOff_(uint8_t note)
{
    for(size_t i = 0; i < kMaxVoices; i++)
    {
        Voice& v = voices_[i];
        if(v.midi_note != note)
            continue;
        if(v.state == VoiceState::Playing || v.state == VoiceState::Releasing)
        {
            v.state = VoiceState::Releasing;
            MarkPolyPortoReleasedSource_(v);
            const uint8_t layer = v.source_layer & 1u;
            SetEnvelopeRelease(v.env_stage,
                               v.env_level,
                               v.env_r_step,
                               v.loop_voice ? loop_env_release_ms_[layer]
                                            : kDefaultEnvReleaseMs,
                               sample_rate_);
            if(!v.loop_voice)
                v.gate = false;
            v.dir  = 1;
        }
        else if(v.state == VoiceState::StealFadeOut)
        {
            if(v.new_loop_voice)
            {
                const uint8_t layer = v.new_source_layer & 1u;
                v.poly_porto_source_valid = true;
                v.poly_porto_source_released = true;
                v.poly_porto_source_note = v.note;
                v.poly_porto_source_layer = v.new_source_layer & 1u;
                v.poly_porto_source_order = ++poly_porto_source_order_counter_;
                v.poly_porto_release_sample_time = audio_sample_counter_;
                SetEnvelopeRelease(v.new_env_stage,
                                   v.new_env_level,
                                   v.new_env_r_step,
                                   loop_env_release_ms_[layer],
                                   sample_rate_);
                v.new_dir  = 1;
            }
            else
            {
                StartStopFade_(v);
                v.pos_frame = v.new_pos_frame;
                v.pos_frac  = v.new_pos_frac;
                v.gain  = v.new_gain;
                v.ratio = v.new_ratio;
                v.source_layer = v.new_source_layer;
                v.fade_in      = 1.0f;
                v.fade_in_step = 0.0f;
                v.loop_voice = v.new_loop_voice;
                SetEnvelopeRelease(v.new_env_stage,
                                   v.new_env_level,
                                   v.new_env_r_step,
                                   kDefaultEnvReleaseMs,
                                   sample_rate_);
                v.new_gate = false;
                v.new_dir  = 1;
                v.poly_porto_source_valid = true;
                v.poly_porto_source_released = true;
                v.poly_porto_source_note = v.note;
                v.poly_porto_source_layer = v.new_source_layer & 1u;
                v.poly_porto_source_order = ++poly_porto_source_order_counter_;
                v.poly_porto_release_sample_time = audio_sample_counter_;
            }
        }
    }
}
