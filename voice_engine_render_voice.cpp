#include "voice_engine_render_internal.h"

namespace
{
void VoiceRender_UpdatePlayheadMetric(float* playhead_metric,
                                      uint32_t* playhead_frame,
                                      uint32_t* playhead_active,
                                      uint8_t ui_layer,
                                      float pos,
                                      float env_level,
                                      uint32_t sample_length)
{
    const float metric = (env_level > 0.0f) ? env_level : 0.0f;
    if(metric >= playhead_metric[ui_layer])
    {
        playhead_metric[ui_layer] = metric;
        const float p = VoicePlayback_ClampPlayheadPos(pos, sample_length);
        playhead_frame[ui_layer] = static_cast<uint32_t>(p);
        playhead_active[ui_layer] = 1u;
    }
}
} // namespace

bool VoiceEngine::StopFade_AdvanceAndFinishIfDone_(Voice& v, StopFadeState& sf)
{
    if(!sf.active)
        return false;
    sf.remaining--;
    sf.level -= sf.step;
    if(sf.level < 0.0f)
        sf.level = 0.0f;
    if(sf.remaining <= 0)
    {
        FinishStopFade_(v);
        return true;
    }
    return false;
}

void VoiceEngine::BeginStopFadeOnStreamEnd_(Voice& v, StopFadeState& sf)
{
    if(sf.active)
        return;
    StartStopFade_(v);
    sf.active = v.stop_fade_active;
    sf.remaining = v.stop_fade_samples_remaining;
    sf.level = v.stop_fade_level;
    sf.step = v.stop_fade_step;
}

void VoiceEngine::CompleteStealXFade_(Voice& v)
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

void VoiceEngine::VoiceRender_PlayheadMetricIfAudible_(const Voice& v,
                                                      const RenderVoiceContext& ctx,
                                                      uint8_t ui_layer,
                                                      float pos,
                                                      float env_level)
{
    if(v.state == VoiceState::Idle || v.sample == nullptr || v.sample->length == 0)
        return;
    VoiceRender_UpdatePlayheadMetric(ctx.playhead_metric,
                                     ctx.playhead_frame,
                                     ctx.playhead_active,
                                     ui_layer,
                                     pos,
                                     env_level,
                                     v.sample->length);
}

void VoiceEngine::CommitNormalVoiceLoopState_(Voice& v, const RenderNormalVoiceLoopState& st)
{
    v.pos = st.pos;
    v.fade_in = st.fade;
    v.env_stage = st.env_stage;
    v.env_level = st.env_level;
    v.env_r_step = st.env_r_step;
    v.gate = st.gate;
    v.dir = st.dir;
    v.stop_fade_active = st.stop_fade.active;
    v.stop_fade_samples_remaining = st.stop_fade.remaining;
    v.stop_fade_level = st.stop_fade.level;
    v.stop_fade_step = st.stop_fade.step;
}

void VoiceEngine::ResolveEffectivePlaybackRegion_(const Voice& v,
                                                  const RenderVoiceContext& ctx,
                                                  EffectivePlaybackRegion& out,
                                                  bool& loop_enabled_base)
{
    out.use_edit = (ctx.edit_sample != nullptr && v.sample == ctx.edit_sample);
    SampleEdit e = ctx.edit;
    out.start = 0;
    out.end = v.sample->length;
    out.ls_i = v.sample->loop_start;
    out.le_i = v.sample->loop_end;
    loop_enabled_base = v.sample->loop_enabled;
    out.edit_gain = 1.0f;
    if(out.use_edit)
    {
        SampleEdit_Clamp(e, v.sample->length);
        out.start = e.start_frame;
        out.end = e.end_frame;
        out.ls_i = e.loop_start;
        out.le_i = e.loop_end;
        loop_enabled_base = (e.loop_enable != 0);
        out.edit_gain = e.gain;
    }
}

bool VoiceEngine::RenderStealXFade_ProcessOneSample_(Voice& v,
                                                     const RenderVoiceContext& ctx,
                                                     const RenderStealXFadeSetup& setup,
                                                     size_t i,
                                                     RenderStealXFadeLoopState& st)
{
    const float x_clamped = VoicePlayback_ClampMixToOne(st.xfade_pos);

    float s_old = 0.0f;
    float s_new = 0.0f;
    bool old_used_seam_xfade = false;
    bool new_used_seam_xfade = false;
    s_old = VoiceRenderFetch_VoiceStream(v.sample,
                                         st.old_pos,
                                         setup.old_gain,
                                         setup.loop_voice,
                                         setup.start,
                                         setup.end,
                                         setup.old_seam_frames,
                                         loop_crossfade_shape_[setup.old_layer],
                                         sample_rate_,
                                         old_used_seam_xfade,
                                         setup.use_edit,
                                         setup.old_loop_enabled,
                                         setup.ls_i,
                                         setup.le_i,
                                         st.old_gate);
    s_new = VoiceRenderFetch_VoiceStream(v.sample,
                                         st.new_pos,
                                         setup.new_gain,
                                         setup.new_loop_voice,
                                         setup.start,
                                         setup.end,
                                         setup.new_seam_frames,
                                         loop_crossfade_shape_[setup.new_layer],
                                         sample_rate_,
                                         new_used_seam_xfade,
                                         setup.use_edit,
                                         setup.new_loop_enabled,
                                         setup.ls_i,
                                         setup.le_i,
                                         st.new_gate);
    s_old = VoiceRenderLoop_ApplyBoundaryFadeNoSeam(s_old,
                                                    setup.loop_voice,
                                                    setup.old_seam_frames,
                                                    old_used_seam_xfade,
                                                    st.old_pos,
                                                    setup.start,
                                                    setup.end,
                                                    sample_rate_);
    s_new = VoiceRenderLoop_ApplyBoundaryFadeNoSeam(s_new,
                                                    setup.new_loop_voice,
                                                    setup.new_seam_frames,
                                                    new_used_seam_xfade,
                                                    st.new_pos,
                                                    setup.start,
                                                    setup.end,
                                                    sample_rate_);
    s_new *= st.new_env_level;
    const float fin_new = VoicePlayback_FadeInMultiplier(st.new_fade_in);
    s_new *= fin_new;

    const float old_mix = s_old * (1.0f - x_clamped);
    const float new_mix = s_new * x_clamped;
    float* old_bus = (setup.old_layer == 0u) ? ctx.outL : ctx.outR;
    float* new_bus = (setup.new_layer == 0u) ? ctx.outL : ctx.outR;
    if(st.stop_fade.active)
    {
        old_bus[i] += old_mix * st.stop_fade.level;
        new_bus[i] += new_mix * st.stop_fade.level;
    }
    else
    {
        old_bus[i] += old_mix;
        new_bus[i] += new_mix;
    }

    if(StopFade_AdvanceAndFinishIfDone_(v, st.stop_fade))
        return true;

    if(!AdvancePos(st.old_pos,
                   st.old_dir,
                   setup.old_ratio,
                   setup.length_f,
                   setup.ls,
                   setup.le,
                   setup.old_loop_enabled,
                   st.old_gate,
                   setup.old_loop_mode,
                   setup.loop_voice ? static_cast<float>(setup.old_seam_frames) : 0.0f))
    {
        st.old_gate = false;
    }
    if(!AdvancePos(st.new_pos,
                   st.new_dir,
                   setup.new_ratio,
                   setup.length_f,
                   setup.ls,
                   setup.le,
                   setup.new_loop_enabled,
                   st.new_gate,
                   setup.new_loop_mode,
                   setup.new_loop_voice ? static_cast<float>(setup.new_seam_frames) : 0.0f))
    {
        st.new_env_stage = EnvStage::Off;
        st.new_env_level = 0.0f;
        st.new_gate = false;
        const bool need_stop_fade = !st.stop_fade.active;
        BeginStopFadeOnStreamEnd_(v, st.stop_fade);
        if(need_stop_fade)
        {
            st.old_gate = false;
            st.new_gate = false;
        }
    }

    VoicePlayback_ClampPosPastEndWhenGateOff(setup.length_f, st.old_gate, st.old_pos);
    VoicePlayback_ClampPosPastEndWhenGateOff(setup.length_f, st.new_gate, st.new_pos);

    st.xfade_pos += setup.x_step;
    if(st.new_fade_in < 1.0f)
        VoicePlayback_StepFadeIn(st.new_fade_in, setup.new_fade_step);

    StepEnvelope(st.new_env_stage,
                 st.new_env_level,
                 setup.new_env_a_step,
                 setup.new_env_d_step,
                 setup.new_env_sustain,
                 st.new_env_r_step);
    if(st.new_env_stage == EnvStage::Off)
        st.new_env_level = 0.0f;
    return false;
}

void VoiceEngine::RenderStealXFadeVoice_(Voice& v,
                                         const RenderVoiceContext& ctx,
                                         float pitch_scale,
                                         StopFadeState& stop_fade)
{
    if(v.sample == nullptr || v.sample->pcm == nullptr || v.sample->length == 0)
    {
        v.state = VoiceState::Idle;
        return;
    }

    EffectivePlaybackRegion r{};
    bool old_loop_enabled = false;
    ResolveEffectivePlaybackRegion_(v, ctx, r, old_loop_enabled);
    bool new_loop_enabled = old_loop_enabled;
    RenderStealXFadeSetup setup{};
    setup.start = r.start;
    setup.end = r.end;
    setup.ls_i = r.ls_i;
    setup.le_i = r.le_i;
    setup.use_edit = r.use_edit;
    setup.edit_gain = r.edit_gain;
    setup.old_layer = v.old_source_layer & 1u;
    setup.new_layer = v.new_source_layer & 1u;
    setup.old_loop_enabled = old_loop_enabled;
    setup.new_loop_enabled = new_loop_enabled;
    setup.loop_voice = v.loop_voice;
    setup.new_loop_voice = v.new_loop_voice;
    if(setup.loop_voice)
    {
        setup.old_loop_enabled = true;
        setup.ls_i = setup.start;
        setup.le_i = setup.end;
    }
    if(setup.new_loop_voice)
    {
        setup.new_loop_enabled = true;
        setup.ls_i = setup.start;
        setup.le_i = setup.end;
    }
    setup.length_f = static_cast<float>(setup.end);
    setup.ls = static_cast<float>(setup.ls_i);
    setup.le = static_cast<float>(setup.le_i);
    RenderStealXFadeLoopState st{};
    st.old_pos = v.old_pos;
    st.new_pos = v.new_pos;
    st.old_gate = v.old_gate;
    st.new_gate = v.new_gate;
    st.old_dir = v.old_dir;
    st.new_dir = v.new_dir;
    st.xfade_pos = v.xfade_pos;
    st.new_fade_in = v.new_fade_in;
    st.new_env_stage = v.new_env_stage;
    st.new_env_level = v.new_env_level;
    st.new_env_r_step = v.new_env_r_step;
    st.stop_fade = stop_fade;

    VoicePlayback_NormalizeStealXFadePositions(setup.length_f,
                                             setup.ls,
                                             setup.start,
                                             setup.old_loop_enabled,
                                             setup.new_loop_enabled,
                                             st.old_pos,
                                             st.new_pos,
                                             st.old_gate,
                                             st.new_gate);

    setup.old_ratio = v.old_ratio * ctx.engine_tune_scale[setup.old_layer] * pitch_scale;
    setup.new_ratio = v.new_ratio * ctx.engine_tune_scale[setup.new_layer] * pitch_scale;
    setup.old_gain = v.old_gain * setup.edit_gain * ctx.engine_voice_gain[setup.old_layer];
    setup.new_gain = v.new_gain * setup.edit_gain * ctx.engine_voice_gain[setup.new_layer];
    setup.old_seam_frames
        = setup.loop_voice ? ComputeLoopSeamCrossfadeFrames(setup.start,
                                                            setup.end,
                                                            loop_crossfade_amount_[setup.old_layer])
                           : 0u;
    setup.new_seam_frames
        = setup.new_loop_voice ? ComputeLoopSeamCrossfadeFrames(setup.start,
                                                                setup.end,
                                                                loop_crossfade_amount_[setup.new_layer])
                               : 0u;
    setup.old_loop_mode = setup.loop_voice ? LoopMode::Forward : ctx.loop_mode;
    setup.new_loop_mode = setup.new_loop_voice ? LoopMode::Forward : ctx.loop_mode;
    setup.x_step = v.xfade_step;
    setup.new_fade_step = v.new_fade_in_step;
    setup.new_env_a_step = v.new_env_a_step;
    setup.new_env_d_step = v.new_env_d_step;
    setup.new_env_sustain = v.new_env_sustain;

    if(st.stop_fade.active)
    {
        st.old_gate = false;
        st.new_gate = false;
    }

    for(size_t i = 0; i < ctx.size; i++)
    {
        if(RenderStealXFade_ProcessOneSample_(v, ctx, setup, i, st))
            break;
    }

    if(v.state == VoiceState::Idle)
        return;

    v.old_pos   = st.old_pos;
    v.new_pos   = st.new_pos;
    v.xfade_pos  = st.xfade_pos;
    v.new_fade_in = st.new_fade_in;
    v.new_env_stage = st.new_env_stage;
    v.new_env_level = st.new_env_level;
    v.new_env_r_step = st.new_env_r_step;
    v.old_gate = st.old_gate;
    v.old_dir  = st.old_dir;
    v.new_gate = st.new_gate;
    v.new_dir  = st.new_dir;
    v.stop_fade_active = st.stop_fade.active;
    v.stop_fade_samples_remaining = st.stop_fade.remaining;
    v.stop_fade_level = st.stop_fade.level;
    v.stop_fade_step = st.stop_fade.step;

    stop_fade = st.stop_fade;

    if(v.xfade_pos >= 1.0f)
        CompleteStealXFade_(v);

    VoiceRender_PlayheadMetricIfAudible_(v,
                                         ctx,
                                         v.new_source_layer & 1u,
                                         v.new_pos,
                                         st.new_env_level);
}

bool VoiceEngine::RenderNormalVoice_ProcessOneSample_(Voice& v,
                                                      const RenderVoiceContext& ctx,
                                                      const RenderNormalVoicePerBlockSetup& setup,
                                                      size_t i,
                                                      RenderNormalVoiceLoopState& st)
{
    bool used_seam_xfade = false;
    float s = VoiceRenderFetch_VoiceStream(v.sample,
                                           st.pos,
                                           setup.gain,
                                           setup.loop_voice,
                                           setup.start,
                                           setup.end,
                                           setup.seam_frames,
                                           loop_crossfade_shape_[setup.source_layer],
                                           sample_rate_,
                                           used_seam_xfade,
                                           setup.use_edit,
                                           setup.loop_enabled,
                                           setup.ls_i,
                                           setup.le_i,
                                           st.gate);
    s = VoiceRenderLoop_ApplyBoundaryFadeNoSeam(s,
                                                setup.loop_voice,
                                                setup.seam_frames,
                                                used_seam_xfade,
                                                st.pos,
                                                setup.start,
                                                setup.end,
                                                sample_rate_);
    s *= st.env_level;
    const float fin = VoicePlayback_FadeInMultiplier(st.fade);
    s *= fin;
    if(st.stop_fade.active)
        s *= st.stop_fade.level;
    float* layer_bus = (setup.source_layer == 0u) ? ctx.outL : ctx.outR;
    layer_bus[i] += s;

    if(StopFade_AdvanceAndFinishIfDone_(v, st.stop_fade))
        return true;

    if(!AdvancePos(st.pos,
                   st.dir,
                   setup.ratio,
                   setup.length_f,
                   setup.ls,
                   setup.le,
                   setup.loop_enabled,
                   st.gate,
                   setup.voice_loop_mode,
                   setup.loop_voice ? static_cast<float>(setup.seam_frames) : 0.0f))
    {
        const bool need_stop_fade = !st.stop_fade.active;
        BeginStopFadeOnStreamEnd_(v, st.stop_fade);
        if(need_stop_fade)
            st.gate = false;
        VoicePlayback_ClampPosToLastFrameIfValid(setup.length_f, st.pos);
    }
    if(st.fade < 1.0f)
        VoicePlayback_StepFadeIn(st.fade, setup.fade_step);

    StepEnvelope(st.env_stage,
                 st.env_level,
                 setup.env_a_step,
                 setup.env_d_step,
                 setup.env_sustain,
                 st.env_r_step);
    if(st.env_stage == EnvStage::Off && !st.stop_fade.active)
    {
        v.state = VoiceState::Idle;
        return true;
    }
    return false;
}

void VoiceEngine::RenderNormalVoice_(Voice& v,
                                     const RenderVoiceContext& ctx,
                                     float pitch_scale,
                                     StopFadeState& stop_fade)
{
    if(v.sample == nullptr || v.sample->pcm == nullptr || v.sample->length == 0)
    {
        v.state = VoiceState::Idle;
        return;
    }

    const uint8_t source_layer = v.source_layer & 1u;
    const float ratio = v.ratio * ctx.engine_tune_scale[source_layer] * pitch_scale;
    EffectivePlaybackRegion r{};
    bool loop_enabled = false;
    ResolveEffectivePlaybackRegion_(v, ctx, r, loop_enabled);
    const uint32_t start = r.start;
    const uint32_t end = r.end;
    uint32_t ls_i = r.ls_i;
    uint32_t le_i = r.le_i;
    const bool use_edit = r.use_edit;
    const float edit_gain = r.edit_gain;
    const bool loop_voice = v.loop_voice;
    if(loop_voice)
    {
        loop_enabled = true;
        ls_i = start;
        le_i = end;
    }
    const float gain = v.gain * edit_gain * ctx.engine_voice_gain[source_layer];
    const float length_f = static_cast<float>(end);
    const float ls = static_cast<float>(ls_i);
    const float le = static_cast<float>(le_i);
    const uint32_t seam_frames
        = loop_voice ? ComputeLoopSeamCrossfadeFrames(start,
                                                      end,
                                                      loop_crossfade_amount_[source_layer])
                     : 0u;
    const LoopMode voice_loop_mode = loop_voice ? LoopMode::Forward : ctx.loop_mode;

    RenderNormalVoicePerBlockSetup setup{};
    setup.start = start;
    setup.end = end;
    setup.length_f = length_f;
    setup.ls = ls;
    setup.le = le;
    setup.ls_i = ls_i;
    setup.le_i = le_i;
    setup.loop_enabled = loop_enabled;
    setup.loop_voice = loop_voice;
    setup.seam_frames = seam_frames;
    setup.voice_loop_mode = voice_loop_mode;
    setup.source_layer = source_layer;
    setup.gain = gain;
    setup.use_edit = use_edit;
    setup.ratio = ratio;
    setup.fade_step = v.fade_in_step;
    setup.env_a_step = v.env_a_step;
    setup.env_d_step = v.env_d_step;
    setup.env_sustain = v.env_sustain;

    RenderNormalVoiceLoopState st{};
    st.pos = v.pos;
    st.gate = v.gate;
    st.dir = v.dir;
    st.fade = v.fade_in;
    st.env_stage = v.env_stage;
    st.env_level = v.env_level;
    st.env_r_step = v.env_r_step;
    st.stop_fade = stop_fade;
    if(st.stop_fade.active)
        st.gate = false;
    VoicePlayback_NormalizeVoiceBlockStart(setup.length_f, setup.ls, setup.start, setup.loop_enabled, st.gate, st.pos);

    for(size_t i = 0; i < ctx.size; i++)
    {
        if(RenderNormalVoice_ProcessOneSample_(v, ctx, setup, i, st))
            break;
    }

    stop_fade = st.stop_fade;

    if(v.state != VoiceState::Idle)
    {
        CommitNormalVoiceLoopState_(v, st);

        VoiceRender_PlayheadMetricIfAudible_(v, ctx, source_layer & 1u, st.pos, st.env_level);
    }
}
