#include "voice_engine_internal.h"

namespace
{
static constexpr float kVoiceAmpScale = 0.15f; // keep headroom for 10 voices + FX
static constexpr float kStealFadeOutMs = 1.25f;
}

void VoiceEngine::SetPolyPortoEnabled(uint8_t layer, bool enabled)
{
    poly_porto_enabled_[layer & 1u] = enabled;
}

void VoiceEngine::SetPolyPortoVoiceLimit(uint8_t layer, uint8_t voice_limit)
{
    layer &= 1u;
    if(voice_limit < kExpressPolyPortoVoicesMin)
        voice_limit = kExpressPolyPortoVoicesMin;
    if(voice_limit > kExpressPolyPortoVoicesMax)
        voice_limit = kExpressPolyPortoVoicesMax;
    poly_porto_voice_limit_[layer] = voice_limit;
}

void VoiceEngine::SetPolyPortoSlideMs(uint8_t layer, float slide_ms)
{
    layer &= 1u;
    if(slide_ms < static_cast<float>(kExpressPolyPortoSlideMinMs))
        slide_ms = static_cast<float>(kExpressPolyPortoSlideMinMs);
    if(slide_ms > static_cast<float>(kExpressPolyPortoSlideMaxMs))
        slide_ms = static_cast<float>(kExpressPolyPortoSlideMaxMs);
    poly_porto_slide_ms_[layer] = slide_ms;
}

void VoiceEngine::SetPolyPortoSourceRangeSemitones(uint8_t layer, uint8_t semitones)
{
    layer &= 1u;
    if(semitones < kExpressPolyPortoRangeMinSemitones)
        semitones = kExpressPolyPortoRangeMinSemitones;
    if(semitones > kExpressPolyPortoRangeMaxSemitones)
        semitones = kExpressPolyPortoRangeMaxSemitones;
    poly_porto_source_range_semitones_[layer] = semitones;
}

void VoiceEngine::SetPolyPortoSourceMode(uint8_t layer, uint8_t source_mode)
{
    layer &= 1u;
    if(source_mode > kExpressPolyPortoSourceLatest)
        source_mode = kExpressPolyPortoSourceClosest;
    poly_porto_source_mode_[layer] = source_mode;
}

void VoiceEngine::SetPolyPortoReleaseMs(uint8_t layer, float release_ms)
{
    layer &= 1u;
    if(release_ms < static_cast<float>(kExpressPolyPortoReleaseMinMs))
        release_ms = static_cast<float>(kExpressPolyPortoReleaseMinMs);
    if(release_ms > static_cast<float>(kExpressPolyPortoReleaseMaxMs))
        release_ms = static_cast<float>(kExpressPolyPortoReleaseMaxMs);
    poly_porto_release_ms_[layer] = release_ms;
}

void VoiceEngine::ClearPolyPortoVoice_(Voice& v)
{
    v.poly_porto_glide_active = false;
    v.poly_porto_managed = false;
    v.poly_porto_source_valid = false;
    v.poly_porto_source_released = false;
    v.poly_porto_source_note = 0u;
    v.poly_porto_source_layer = 0u;
    v.poly_porto_slide_samples_remaining = 0u;
    v.poly_porto_current_semitones = 0.0f;
    v.poly_porto_target_semitones = 0.0f;
    v.poly_porto_step_semitones = 0.0f;
    v.poly_porto_source_order = 0u;
    v.poly_porto_release_sample_time = 0u;
}

void VoiceEngine::MarkPolyPortoHeldSource_(Voice& v)
{
    const uint8_t layer = v.source_layer & 1u;
    if(v.poly_porto_source_valid && !v.poly_porto_source_released
       && v.poly_porto_source_note == v.note
       && v.poly_porto_source_layer == layer)
    {
        return;
    }

    v.poly_porto_source_valid = true;
    v.poly_porto_source_released = false;
    v.poly_porto_source_note = v.note;
    v.poly_porto_source_layer = layer;
    v.poly_porto_source_order = ++poly_porto_source_order_counter_;
    v.poly_porto_release_sample_time = 0u;
}

void VoiceEngine::MarkPolyPortoReleasedSource_(Voice& v)
{
    const uint8_t layer = v.source_layer & 1u;
    if(v.poly_porto_source_valid && v.poly_porto_source_released
       && v.poly_porto_source_note == v.note
       && v.poly_porto_source_layer == layer)
    {
        return;
    }

    v.poly_porto_source_valid = true;
    v.poly_porto_source_released = true;
    v.poly_porto_source_note = v.note;
    v.poly_porto_source_layer = layer;
    v.poly_porto_source_order = ++poly_porto_source_order_counter_;
    v.poly_porto_release_sample_time = audio_sample_counter_;
}

void VoiceEngine::BeginPolyPortoGlide_(Voice& v, uint8_t from_note, uint8_t to_note, float slide_ms)
{
    BeginPolyPortoGlideFromAbsoluteNote_(v, static_cast<float>(from_note), to_note, slide_ms);
}

void VoiceEngine::BeginPolyPortoGlideFromAbsoluteNote_(Voice& v,
                                                       float from_note,
                                                       uint8_t to_note,
                                                       float slide_ms)
{
    if(v.sample == nullptr)
        return;

    const float from_semitones = from_note - static_cast<float>(v.sample->root_key);
    const float to_semitones = static_cast<float>((int)to_note - (int)v.sample->root_key);
    int glide_samples = static_cast<int>(sample_rate_ * 0.001f * slide_ms + 0.5f);
    if(glide_samples < 1)
        glide_samples = 1;
    if(glide_samples > 65535)
        glide_samples = 65535;

    v.poly_porto_glide_active = true;
    v.poly_porto_managed = true;
    v.poly_porto_current_semitones = from_semitones;
    v.poly_porto_target_semitones = to_semitones;
    v.poly_porto_step_semitones = (to_semitones - from_semitones) / static_cast<float>(glide_samples);
    v.poly_porto_slide_samples_remaining = static_cast<uint16_t>(glide_samples);
    v.ratio = ComputeRatioFromSemitoneDelta(from_semitones);
}

float VoiceEngine::CurrentPolyPortoSourceAbsoluteNote_(const Voice& v) const
{
    if(v.sample == nullptr)
        return static_cast<float>(v.note);
    if(v.poly_porto_glide_active)
        return static_cast<float>(v.sample->root_key) + v.poly_porto_current_semitones;
    return static_cast<float>(v.note);
}

bool VoiceEngine::VoiceEligibleAsPolyPortoSource_(const Voice& v,
                                                  uint8_t source_layer,
                                                  uint8_t note,
                                                  uint8_t range_semitones,
                                                  float release_ms) const
{
    if(!v.poly_porto_source_valid)
        return false;
    if(v.state == VoiceState::Idle || v.state == VoiceState::StealFadeOut)
        return false;
    if(v.stop_fade_active)
        return false;
    if((v.poly_porto_source_layer & 1u) != (source_layer & 1u))
        return false;

    const float source_note = CurrentPolyPortoSourceAbsoluteNote_(v);
    const float diff = (source_note >= static_cast<float>(note))
                           ? (source_note - static_cast<float>(note))
                           : (static_cast<float>(note) - source_note);
    if(diff > static_cast<float>(range_semitones))
        return false;

    if(!v.poly_porto_source_released)
        return v.state == VoiceState::Playing;

    if(v.state != VoiceState::Releasing)
        return false;

    uint64_t max_age = static_cast<uint64_t>(sample_rate_ * 0.001f * release_ms + 0.5f);
    if(max_age == 0u)
        max_age = 1u;
    const uint64_t age = audio_sample_counter_ - v.poly_porto_release_sample_time;
    return age <= max_age;
}

uint8_t VoiceEngine::CountActivePolyPortoVoices_(uint8_t source_layer, int exclude_index) const
{
    source_layer &= 1u;
    uint8_t count = 0u;
    for(size_t i = 0; i < kMaxVoices; ++i)
    {
        if(static_cast<int>(i) == exclude_index)
            continue;
        const Voice& v = voices_[i];
        if(v.state == VoiceState::Idle)
            continue;
        if((v.source_layer & 1u) != source_layer)
            continue;
        if(v.poly_porto_managed && v.poly_porto_glide_active)
            ++count;
    }
    return count;
}

int VoiceEngine::FindPolyPortoSourceVoice_(uint8_t source_layer,
                                           uint8_t note,
                                           uint8_t range_semitones,
                                           uint8_t source_mode,
                                           float release_ms) const
{
    source_layer &= 1u;
    int best_idx = -1;
    float best_diff = 1.0e9f;
    uint32_t best_order = 0u;
    for(size_t i = 0; i < kMaxVoices; ++i)
    {
        const Voice& v = voices_[i];
        if(!VoiceEligibleAsPolyPortoSource_(v, source_layer, note, range_semitones, release_ms))
            continue;

        const float source_note = CurrentPolyPortoSourceAbsoluteNote_(v);
        const float diff = (source_note >= static_cast<float>(note))
                               ? (source_note - static_cast<float>(note))
                               : (static_cast<float>(note) - source_note);
        if(source_mode == kExpressPolyPortoSourceLatest)
        {
            if(best_idx < 0 || v.poly_porto_source_order > best_order
               || (v.poly_porto_source_order == best_order && diff < best_diff))
            {
                best_idx = static_cast<int>(i);
                best_diff = diff;
                best_order = v.poly_porto_source_order;
            }
            continue;
        }

        if(best_idx < 0 || diff < best_diff
           || (diff == best_diff && v.poly_porto_source_order > best_order))
        {
            best_idx = static_cast<int>(i);
            best_diff = diff;
            best_order = v.poly_porto_source_order;
        }
    }
    return best_idx;
}

bool VoiceEngine::TryStartPolyPortoVoice_(const Sample* sample,
                                          uint8_t note,
                                          uint8_t velocity,
                                          uint8_t source_layer,
                                          uint8_t vel_layer,
                                          uint32_t start_id,
                                          uint8_t& out_index)
{
    out_index = 0u;
    source_layer &= 1u;
    if(!poly_porto_enabled_[source_layer] || sample == nullptr)
        return false;

    const int candidate_idx = FindPolyPortoSourceVoice_(source_layer,
                                                        note,
                                                        poly_porto_source_range_semitones_[source_layer],
                                                        poly_porto_source_mode_[source_layer],
                                                        poly_porto_release_ms_[source_layer]);
    if(candidate_idx < 0)
        return false;

    if(CountActivePolyPortoVoices_(source_layer, -1)
       >= poly_porto_voice_limit_[source_layer])
        return false;

    bool stole = false;
    uint8_t stolen_index = 0u;
    uint32_t stolen_start_id = 0u;
    const int idx = AllocateVoiceExcluding_(source_layer,
                                            candidate_idx,
                                            stole,
                                            stolen_index,
                                            stolen_start_id);
    if(idx < 0)
        return false;

    const float from_note = CurrentPolyPortoSourceAbsoluteNote_(voices_[static_cast<size_t>(candidate_idx)]);
    Voice& v = voices_[static_cast<size_t>(idx)];

    if(stole)
    {
        const bool already_fading = (v.state == VoiceState::StealFadeOut);

        v.sample = sample;
        v.vel_layer = vel_layer;
        v.mod_env.Trigger(env_attack_ms_, env_decay_ms_);
        v.stop_fade_active = false;
        v.stop_fade_samples_remaining = 0;
        v.stop_fade_level = 0.0f;
        v.stop_fade_step = 0.0f;

        if(!already_fading)
        {
            v.old_pos_frame = v.pos_frame;
            v.old_pos_frac = v.pos_frac;
            v.old_ratio = v.ratio;
            const float old_fin = (v.fade_in < 1.0f) ? v.fade_in : 1.0f;
            const float old_env = (v.env_level < 1.0f) ? v.env_level : 1.0f;
            v.old_gain = v.gain * old_fin * old_env;
            v.old_source_layer = v.source_layer;
            v.old_gate = v.gate;
            v.old_dir = v.dir;
        }

        v.new_pos_frame = 0u;
        v.new_pos_frac = 0.0f;
        SampleEdit e{};
        if(LookupSampleEdit_(sample, e))
        {
            SampleEdit_Clamp(e, sample->length);
            v.new_pos_frame = e.start_frame;
        }
        v.new_ratio = ComputeRatio(note, sample->root_key);
        // Velocity-independent base gain (inherent vel tracking off).
        v.new_gain = kVoiceAmpScale;
        v.new_source_layer = source_layer;
        v.new_loop_voice = engine_loop_enabled_[source_layer];
        v.new_gate = true;
        v.new_dir = 1;
        // Mode-aware (matches StartVoice_): attack/release from the perform
        // ADSR in both modes; one-shot holds sustain at full. Resolved release
        // stashed for note-off.
        v.resolved_release_ms = loop_env_release_ms_[source_layer];
        InitEnvelope(v.new_env_stage,
                     v.new_env_level,
                     v.new_env_a_step,
                     v.new_env_d_step,
                     v.new_env_r_step,
                     v.new_env_sustain,
                     loop_env_attack_ms_[source_layer],
                     loop_env_decay_ms_[source_layer],
                     v.new_loop_voice ? loop_env_sustain_level_[source_layer] : 1.0f,
                     loop_env_release_ms_[source_layer],
                     sample_rate_);

        if(!already_fading)
        {
            int n = static_cast<int>(sample_rate_ * 0.001f * kStealFadeOutMs);
            if(n < 1)
                n = 1;
            v.steal_fade_level = 1.0f;
            v.steal_fade_step = 1.0f / static_cast<float>(n);
        }

        v.state = VoiceState::StealFadeOut;
        v.note = note;
        v.midi_note = note;
        v.velocity = velocity;
        v.start_id = start_id;
        ClearPolyPortoVoice_(v);
        BeginPolyPortoGlideFromAbsoluteNote_(v, from_note, note, poly_porto_slide_ms_[source_layer]);
        v.new_ratio = ComputeRatioFromSemitoneDelta(v.poly_porto_current_semitones);
    }
    else
    {
        StartVoice_(v, sample, note, velocity, source_layer, vel_layer, start_id);
        v.mod_env.Trigger(env_attack_ms_, env_decay_ms_);
        BeginPolyPortoGlideFromAbsoluteNote_(v, from_note, note, poly_porto_slide_ms_[source_layer]);
    }

    out_index = static_cast<uint8_t>(idx);
    return true;
}
