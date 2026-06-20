#include "voice_engine_internal.h"

#include "app_state_shared.h"

static constexpr float kVoiceAmpScale      = 0.15f; // keep headroom for 10 voices + FX
static constexpr float kStealFadeOutMs     = 1.25f;
static constexpr float kLoopBoundaryFadeMs = 1.0f;

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
                const uint8_t source_layer = sample_index & 1u;
                uint8_t vel_layer = static_cast<uint8_t>((e.value >> 8) & 0xFFu);
                if(vel_layer > 1)
                    vel_layer = 1;
                const Sample* sample = nullptr;
                // .bk multisample override: if a .bk multisample is loaded on the
                // single layer, pick the per-note slice instead of the normal
                // sample bank entry. Off-range notes (outside the .bk's
                // [lo_note, hi_note]) skip voice allocation entirely — silence by
                // design.
                const bool bk_layer_b_active
                    = (shared_ != nullptr && shared_->bk_layer_b.loaded);
                if(bk_layer_b_active)
                {
                    const bk::BkFileHeader& hdr = shared_->bk_layer_b.hdr;
                    if(note < hdr.lo_note || note > hdr.hi_note)
                        continue;
                    sample = &shared_->bk_layer_b.slice_sample[note - hdr.lo_note];
                }
                else
                {
                    if(sample_index < sample_bank_count_)
                        sample = sample_bank_[sample_index];
                    if(sample == nullptr)
                        sample = current_sample_;
                }
                if(sample == nullptr || sample->pcm == nullptr || sample->length == 0)
                    continue;

                const uint32_t start_id = ++note_start_counter_;
                uint8_t poly_idx = 0u;
                if(TryStartPolyPortoVoice_(sample,
                                           note,
                                           vel,
                                           source_layer,
                                           vel_layer,
                                           start_id,
                                           poly_idx))
                {
                    if(last_voice_packed_)
                        last_voice_packed_->store(PackVoiceDebug_(poly_idx, note, vel),
                                                  std::memory_order_relaxed);
                    continue;
                }

                bool     stole = false;
                uint8_t  stolen_index = 0;
                uint32_t stolen_start_id = 0;
                const int idx = AllocateVoice_(source_layer, stole, stolen_index, stolen_start_id);
                if(idx >= 0)
                {
                    Voice& v = voices_[(size_t)idx];
                    if(stole)
                    {
                        const bool already_fading = (v.state == VoiceState::StealFadeOut);

                        v.sample       = sample;
                        v.vel_layer    = vel_layer;
                        v.send_active  = false; // sends not applied on steal path
                        v.mod_env.Trigger(env_attack_ms_, env_decay_ms_);
                        v.stop_fade_active = false;
                        v.stop_fade_samples_remaining = 0;
                        v.stop_fade_level = 0.0f;
                        v.stop_fade_step = 0.0f;

                        // Second steal while fading: keep victim tail; replace pending new only.
                        if(!already_fading)
                        {
                            v.old_pos_frame    = v.pos_frame;
                            v.old_pos_frac     = v.pos_frac;
                            v.old_ratio        = v.ratio;
                            const float old_fin = (v.fade_in < 1.0f) ? v.fade_in : 1.0f;
                            const float old_env = (v.env_level < 1.0f) ? v.env_level : 1.0f;
                            v.old_gain         = v.gain * old_fin * old_env;
                            v.old_source_layer = v.source_layer;
                            v.old_gate         = v.gate;
                            v.old_dir          = v.dir;
                        }

                        v.new_pos_frame = 0u;
                        v.new_pos_frac  = 0.0f;
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
                        v.new_dir  = 1;
                        // Mode-aware (matches StartVoice_): attack/release from
                        // the perform ADSR in both modes; one-shot holds sustain
                        // at full. Resolved release stashed for note-off.
                        v.resolved_release_ms = loop_env_release_ms_[source_layer];
                        InitEnvelope(v.new_env_stage,
                                     v.new_env_level,
                                     v.new_env_a_step,
                                     v.new_env_d_step,
                                     v.new_env_r_step,
                                     v.new_env_sustain,
                                     loop_env_attack_ms_[source_layer],
                                     loop_env_decay_ms_[source_layer],
                                     v.new_loop_voice ? loop_env_sustain_level_[source_layer]
                                                      : 1.0f,
                                     loop_env_release_ms_[source_layer],
                                     sample_rate_,
                                     loop_env_attack_log_[source_layer],
                                     loop_env_release_log_[source_layer]);

                        if(!already_fading)
                        {
                            int n = static_cast<int>(sample_rate_ * 0.001f * kStealFadeOutMs);
                            if(n < 1)
                                n = 1;
                            v.steal_fade_level = 1.0f;
                            v.steal_fade_step  = 1.0f / static_cast<float>(n);
                        }

                        v.state    = VoiceState::StealFadeOut;
                        v.note     = note;
                        v.midi_note = note;
                        v.velocity = vel;
                        v.start_id = start_id;
                        v.poly_porto_source_valid = false;
                        v.poly_porto_source_note = note;
                        v.poly_porto_source_layer = source_layer;
                        v.poly_porto_source_order = 0u;
                        v.poly_porto_managed = false;
                        v.poly_porto_glide_active = false;
                    }
                    else
                    {
                        StartVoice_(v, sample, note, vel, source_layer, vel_layer, start_id);
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
