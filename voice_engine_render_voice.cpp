#include "voice_engine_render_internal.h"

void VoiceEngine::RenderStealXFadeVoice_(Voice& v,
                                         const RenderVoiceContext& ctx,
                                         float pitch_scale,
                                         bool& stop_fade_active,
                                         int32_t& stop_fade_remaining,
                                         float& stop_fade_level,
                                         float& stop_fade_step)
{
    if(v.sample == nullptr || v.sample->pcm == nullptr || v.sample->length == 0)
    {
        v.state = VoiceState::Idle;
        return;
    }

    float old_pos = v.old_pos;
    float new_pos = v.new_pos;
    bool old_gate = v.old_gate;
    bool new_gate = v.new_gate;
    int8_t old_dir = v.old_dir;
    int8_t new_dir = v.new_dir;
    bool use_edit = (ctx.edit_sample != nullptr && v.sample == ctx.edit_sample);
    SampleEdit e = ctx.edit;
    uint32_t start = 0;
    uint32_t end = v.sample->length;
    uint32_t ls_i = v.sample->loop_start;
    uint32_t le_i = v.sample->loop_end;
    const uint8_t old_layer = v.old_source_layer & 1u;
    const uint8_t new_layer = v.new_source_layer & 1u;
    bool old_loop_enabled = v.sample->loop_enabled;
    bool new_loop_enabled = v.sample->loop_enabled;
    float edit_gain = 1.0f;
    if(use_edit)
    {
        SampleEdit_Clamp(e, v.sample->length);
        start = e.start_frame;
        end = e.end_frame;
        ls_i = e.loop_start;
        le_i = e.loop_end;
        old_loop_enabled = (e.loop_enable != 0);
        new_loop_enabled = old_loop_enabled;
        edit_gain = e.gain;
    }
    if(v.loop_voice)
    {
        old_loop_enabled = true;
        ls_i = start;
        le_i = end;
    }
    if(v.new_loop_voice)
    {
        new_loop_enabled = true;
        ls_i = start;
        le_i = end;
    }
    const float length_f = static_cast<float>(end);
    const float ls = static_cast<float>(ls_i);
    const float le = static_cast<float>(le_i);
    VoicePlayback_NormalizeStealXFadePositions(length_f,
                                               ls,
                                               start,
                                               old_loop_enabled,
                                               new_loop_enabled,
                                               old_pos,
                                               new_pos,
                                               old_gate,
                                               new_gate);

    const float old_ratio = v.old_ratio * ctx.engine_tune_scale[old_layer] * pitch_scale;
    const float new_ratio = v.new_ratio * ctx.engine_tune_scale[new_layer] * pitch_scale;
    const float old_gain = v.old_gain * edit_gain * ctx.engine_voice_gain[old_layer];
    const float new_gain = v.new_gain * edit_gain * ctx.engine_voice_gain[new_layer];
    const uint32_t old_seam_frames
        = v.loop_voice ? ComputeLoopSeamCrossfadeFrames(start,
                                                        end,
                                                        loop_crossfade_amount_[old_layer])
                       : 0u;
    const uint32_t new_seam_frames
        = v.new_loop_voice ? ComputeLoopSeamCrossfadeFrames(start,
                                                            end,
                                                            loop_crossfade_amount_[new_layer])
                           : 0u;
    const LoopMode old_loop_mode = v.loop_voice ? LoopMode::Forward : ctx.loop_mode;
    const LoopMode new_loop_mode = v.new_loop_voice ? LoopMode::Forward : ctx.loop_mode;
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

    if(stop_fade_active)
    {
        old_gate = false;
        new_gate = false;
    }

    for(size_t i = 0; i < ctx.size; i++)
    {
        const float x_clamped = VoicePlayback_ClampMixToOne(x);

        float s_old = 0.0f;
        float s_new = 0.0f;
        bool old_used_seam_xfade = false;
        bool new_used_seam_xfade = false;
        s_old = VoiceRenderFetch_VoiceStream(v.sample,
                                             old_pos,
                                             old_gain,
                                             v.loop_voice,
                                             start,
                                             end,
                                             old_seam_frames,
                                             loop_crossfade_shape_[old_layer],
                                             sample_rate_,
                                             old_used_seam_xfade,
                                             use_edit,
                                             old_loop_enabled,
                                             ls_i,
                                             le_i,
                                             old_gate);
        s_new = VoiceRenderFetch_VoiceStream(v.sample,
                                             new_pos,
                                             new_gain,
                                             v.new_loop_voice,
                                             start,
                                             end,
                                             new_seam_frames,
                                             loop_crossfade_shape_[new_layer],
                                             sample_rate_,
                                             new_used_seam_xfade,
                                             use_edit,
                                             new_loop_enabled,
                                             ls_i,
                                             le_i,
                                             new_gate);
        s_old = VoiceRenderLoop_ApplyBoundaryFadeNoSeam(s_old,
                                                        v.loop_voice,
                                                        old_seam_frames,
                                                        old_used_seam_xfade,
                                                        old_pos,
                                                        start,
                                                        end,
                                                        sample_rate_);
        s_new = VoiceRenderLoop_ApplyBoundaryFadeNoSeam(s_new,
                                                        v.new_loop_voice,
                                                        new_seam_frames,
                                                        new_used_seam_xfade,
                                                        new_pos,
                                                        start,
                                                        end,
                                                        sample_rate_);
        s_new *= new_env_level;
        const float fin_new = VoicePlayback_FadeInMultiplier(new_fade);
        s_new *= fin_new;

        const float old_mix = s_old * (1.0f - x_clamped);
        const float new_mix = s_new * x_clamped;
        float* old_bus = (old_layer == 0u) ? ctx.outL : ctx.outR;
        float* new_bus = (new_layer == 0u) ? ctx.outL : ctx.outR;
        if(stop_fade_active)
        {
            old_bus[i] += old_mix * stop_fade_level;
            new_bus[i] += new_mix * stop_fade_level;
        }
        else
        {
            old_bus[i] += old_mix;
            new_bus[i] += new_mix;
        }

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
                       old_loop_enabled,
                       old_gate,
                       old_loop_mode,
                       v.loop_voice ? static_cast<float>(old_seam_frames) : 0.0f))
        {
            old_gate = false;
        }
        if(!AdvancePos(new_pos,
                       new_dir,
                       new_ratio,
                       length_f,
                       ls,
                       le,
                       new_loop_enabled,
                       new_gate,
                       new_loop_mode,
                       v.new_loop_voice ? static_cast<float>(new_seam_frames) : 0.0f))
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

        VoicePlayback_ClampPosPastEndWhenGateOff(length_f, old_gate, old_pos);
        VoicePlayback_ClampPosPastEndWhenGateOff(length_f, new_gate, new_pos);

        x += x_step;
        if(new_fade < 1.0f)
            VoicePlayback_StepFadeIn(new_fade, new_fade_step);

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
        return;

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
    v.stop_fade_active = stop_fade_active;
    v.stop_fade_samples_remaining = stop_fade_remaining;
    v.stop_fade_level = stop_fade_level;
    v.stop_fade_step = stop_fade_step;

    if(v.xfade_pos >= 1.0f)
    {
        v.pos  = v.new_pos;
        v.gain = v.new_gain;
        v.ratio = v.new_ratio;
        v.source_layer = v.new_source_layer;
        v.fade_in = v.new_fade_in;
        v.fade_in_step = v.new_fade_in_step;
        v.loop_voice = v.new_loop_voice;
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

    if(v.state != VoiceState::Idle && v.sample != nullptr && v.sample->length > 0)
    {
        const uint8_t ui_layer = v.new_source_layer & 1u;
        const float metric = (new_env_level > 0.0f) ? new_env_level : 0.0f;
        if(metric >= ctx.playhead_metric[ui_layer])
        {
            ctx.playhead_metric[ui_layer] = metric;
            const float p = VoicePlayback_ClampPlayheadPos(v.new_pos, v.sample->length);
            ctx.playhead_frame[ui_layer] = static_cast<uint32_t>(p);
            ctx.playhead_active[ui_layer] = 1u;
        }
    }
}

void VoiceEngine::RenderNormalVoice_(Voice& v,
                                     const RenderVoiceContext& ctx,
                                     float pitch_scale,
                                     bool& stop_fade_active,
                                     int32_t& stop_fade_remaining,
                                     float& stop_fade_level,
                                     float& stop_fade_step)
{
    if(v.sample == nullptr || v.sample->pcm == nullptr || v.sample->length == 0)
    {
        v.state = VoiceState::Idle;
        return;
    }

    float pos = v.pos;
    const uint8_t source_layer = v.source_layer & 1u;
    const float ratio = v.ratio * ctx.engine_tune_scale[source_layer] * pitch_scale;
    bool use_edit = (ctx.edit_sample != nullptr && v.sample == ctx.edit_sample);
    SampleEdit e = ctx.edit;
    uint32_t start = 0;
    uint32_t end = v.sample->length;
    uint32_t ls_i = v.sample->loop_start;
    uint32_t le_i = v.sample->loop_end;
    bool loop_enabled = v.sample->loop_enabled;
    const bool loop_voice = v.loop_voice;
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
    if(loop_voice)
    {
        loop_enabled = true;
        ls_i = start;
        le_i = end;
    }
    const float gain  = v.gain * edit_gain * ctx.engine_voice_gain[source_layer];
    const float length_f = static_cast<float>(end);
    const float ls = static_cast<float>(ls_i);
    const float le = static_cast<float>(le_i);
    const uint32_t seam_frames
        = loop_voice ? ComputeLoopSeamCrossfadeFrames(start,
                                                      end,
                                                      loop_crossfade_amount_[source_layer])
                     : 0u;
    const LoopMode voice_loop_mode = loop_voice ? LoopMode::Forward : ctx.loop_mode;
    bool gate = v.gate;
    int8_t dir = v.dir;
    if(stop_fade_active)
        gate = false;
    VoicePlayback_NormalizeVoiceBlockStart(length_f, ls, start, loop_enabled, gate, pos);
    float fade = v.fade_in;
    const float fade_step = v.fade_in_step;
    EnvStage env_stage = v.env_stage;
    float env_level = v.env_level;
    const float env_a_step = v.env_a_step;
    const float env_d_step = v.env_d_step;
    float env_r_step = v.env_r_step;
    const float env_sustain = v.env_sustain;
    float* layer_bus = (source_layer == 0u) ? ctx.outL : ctx.outR;

    for(size_t i = 0; i < ctx.size; i++)
    {
        bool used_seam_xfade = false;
        float s = VoiceRenderFetch_VoiceStream(v.sample,
                                               pos,
                                               gain,
                                               loop_voice,
                                               start,
                                               end,
                                               seam_frames,
                                               loop_crossfade_shape_[source_layer],
                                               sample_rate_,
                                               used_seam_xfade,
                                               use_edit,
                                               loop_enabled,
                                               ls_i,
                                               le_i,
                                               gate);
        s = VoiceRenderLoop_ApplyBoundaryFadeNoSeam(s,
                                                    loop_voice,
                                                    seam_frames,
                                                    used_seam_xfade,
                                                    pos,
                                                    start,
                                                    end,
                                                    sample_rate_);
        s *= env_level;
        const float fin = VoicePlayback_FadeInMultiplier(fade);
        s *= fin;
        if(stop_fade_active)
            s *= stop_fade_level;
        layer_bus[i] += s;

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
                       voice_loop_mode,
                       loop_voice ? static_cast<float>(seam_frames) : 0.0f))
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
            VoicePlayback_ClampPosToLastFrameIfValid(length_f, pos);
        }
        if(fade < 1.0f)
            VoicePlayback_StepFadeIn(fade, fade_step);

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
        v.stop_fade_active = stop_fade_active;
        v.stop_fade_samples_remaining = stop_fade_remaining;
        v.stop_fade_level = stop_fade_level;
        v.stop_fade_step = stop_fade_step;

        if(v.sample != nullptr && v.sample->length > 0)
        {
            const uint8_t ui_layer = source_layer & 1u;
            const float metric = (env_level > 0.0f) ? env_level : 0.0f;
            if(metric >= ctx.playhead_metric[ui_layer])
            {
                ctx.playhead_metric[ui_layer] = metric;
                const float p = VoicePlayback_ClampPlayheadPos(pos, v.sample->length);
                ctx.playhead_frame[ui_layer] = static_cast<uint32_t>(p);
                ctx.playhead_active[ui_layer] = 1u;
            }
        }
    }
}
