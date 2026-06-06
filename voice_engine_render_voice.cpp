#include "voice_engine_render_internal.h"
#include "storage_limits.h"

namespace
{
// Matches the authoritative constant in voice_engine_playback.cpp. Used by
// the P6 per-block threshold precompute below.
static constexpr float kLoopBoundaryFadeMs = 1.0f;

// P5: any envelope stage with per-sample step <= this threshold spans at
// least 5 ms @ 48 kHz (1/240). When ALL of attack/decay/release step values
// are this slow, the voice runs the envelope once per block and linearly
// ramps env_level per sample. Any stage faster than this keeps the existing
// per-sample StepEnvelope state machine to preserve sub-5-ms sharpness.
static constexpr float kFastEnvelopeStepThreshold = 1.0f / 240.0f;

void VoiceRender_UpdatePlayheadMetric(float* playhead_metric,
                                      uint32_t* playhead_frame,
                                      uint32_t* playhead_active,
                                      uint8_t ui_layer,
                                      uint32_t pos_frame,
                                      float env_level,
                                      uint32_t sample_length)
{
    const float metric = (env_level > 0.0f) ? env_level : 0.0f;
    if(metric >= playhead_metric[ui_layer])
    {
        playhead_metric[ui_layer] = metric;
        playhead_frame[ui_layer]
            = VoicePlayback_ClampPlayheadFrame(pos_frame, sample_length);
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

void VoiceEngine::CompleteStealFadeOut_(Voice& v)
{
    v.pos_frame      = v.new_pos_frame;
    v.pos_frac       = v.new_pos_frac;
    v.gain           = v.new_gain;
    v.ratio          = v.new_ratio;
    v.source_layer   = v.new_source_layer;
    v.fade_in        = 1.0f;
    v.fade_in_step   = 0.0f;
    v.loop_voice     = v.new_loop_voice;
    v.env_stage      = v.new_env_stage;
    v.env_level      = v.new_env_level;
    v.env_a_step     = v.new_env_a_step;
    v.env_d_step     = v.new_env_d_step;
    v.env_r_step     = v.new_env_r_step;
    v.env_sustain    = v.new_env_sustain;
    v.gate           = v.new_gate;
    v.dir            = v.new_dir;
    if(v.poly_porto_glide_active)
        v.ratio = ComputeRatioFromSemitoneDelta(v.poly_porto_current_semitones);
    else
        v.ratio = v.new_ratio;
    v.steal_fade_level = 0.0f;
    v.steal_fade_step  = 0.0f;
    if(v.env_stage == EnvStage::Off)
    {
        v.state = VoiceState::Idle;
        ClearPolyPortoVoice_(v);
    }
    else if(v.env_stage == EnvStage::Release)
    {
        v.state = VoiceState::Releasing;
        if(!v.poly_porto_glide_active)
            v.poly_porto_managed = false;
        MarkPolyPortoReleasedSource_(v);
    }
    else
    {
        v.state = VoiceState::Playing;
        if(!v.poly_porto_glide_active)
            v.poly_porto_managed = false;
        MarkPolyPortoHeldSource_(v);
    }
}

void VoiceEngine::VoiceRender_PlayheadMetricIfAudible_(const Voice& v,
                                                      const RenderVoiceContext& ctx,
                                                      uint8_t ui_layer,
                                                      uint32_t pos_frame,
                                                      float pos_frac,
                                                      float env_level)
{
    if(v.state == VoiceState::Idle || v.sample == nullptr || v.sample->length == 0)
        return;
    (void)pos_frac;
    VoiceRender_UpdatePlayheadMetric(ctx.playhead_metric,
                                     ctx.playhead_frame,
                                     ctx.playhead_active,
                                     ui_layer,
                                     pos_frame,
                                     env_level,
                                     v.sample->length);
}

void VoiceEngine::CommitNormalVoiceLoopState_(Voice& v, const RenderNormalVoiceLoopState& st)
{
    v.pos_frame = st.pos_frame;
    v.pos_frac = st.pos_frac;
    v.fade_in = st.fade;
    v.env_stage = st.env_stage;
    v.env_level = st.env_level;
    v.env_r_step = st.env_r_step;
    v.gate = st.gate;
    v.dir = st.dir;
    v.ratio = st.ratio;
    v.poly_porto_glide_active = st.glide_active;
    v.poly_porto_current_semitones = st.glide_current_semitones;
    v.poly_porto_target_semitones = st.glide_target_semitones;
    v.poly_porto_step_semitones = st.glide_step_semitones;
    v.poly_porto_slide_samples_remaining = st.glide_samples_remaining;
    v.poly_porto_managed = st.glide_active;
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
    // Single bank scan: the slot is reused by the caller's record-preview check
    // and the edit lookup, replacing two separate FindSampleBankSlot_ scans.
    out.slot = FindSampleBankSlot_(v.sample);
    SampleEdit e{};
    out.use_edit = (out.slot >= 0) && sample_edit_valid_[out.slot];
    if(out.use_edit)
        e = sample_edit_bank_[out.slot];
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

bool VoiceEngine::RenderStealFadeOut_ProcessOneSample_(Voice& v,
                                                       const RenderVoiceContext& ctx,
                                                       const RenderStealFadeOutSetup& setup,
                                                       size_t i,
                                                       RenderStealFadeOutLoopState& st)
{
    bool old_used_seam_xfade = false;
    float s = VoiceRenderFetch_VoiceStream(v.sample,
                                           st.old_pos_frame,
                                           st.old_pos_frac,
                                           setup.old_gain,
                                           setup.loop_voice,
                                           setup.start,
                                           setup.end,
                                           setup.old_crossfade_seam_frames,
                                           loop_crossfade_shape_[setup.old_layer],
                                           sample_rate_,
                                           old_used_seam_xfade,
                                           setup.use_edit,
                                           setup.old_loop_enabled,
                                           setup.ls_i,
                                           setup.le_i,
                                           st.old_gate);
    s = VoiceRenderLoop_ApplyBoundaryFadeNoSeam(s,
                                                setup.loop_voice,
                                                setup.old_seam_frames,
                                                old_used_seam_xfade,
                                                st.old_pos_frame,
                                                st.old_pos_frac,
                                                setup.loop_fade_start_threshold,
                                                setup.loop_fade_end_threshold,
                                                setup.start,
                                                setup.end,
                                                sample_rate_);
    const float out = s * st.steal_fade_level;
    float*      bus = (setup.old_layer == 0u) ? ctx.outL : ctx.outR;
    if(st.stop_fade.active)
        bus[i] += out * st.stop_fade.level;
    else
        bus[i] += out;

    if(StopFade_AdvanceAndFinishIfDone_(v, st.stop_fade))
        return true;

    if(!AdvancePos(st.old_pos_frame,
                   st.old_pos_frac,
                   st.old_dir,
                   setup.old_ratio,
                   setup.end,
                   setup.ls_i,
                   setup.le_i,
                   setup.old_loop_enabled,
                   st.old_gate,
                   setup.old_loop_mode,
                   setup.loop_voice ? setup.old_seam_frames : 0u))
    {
        st.old_gate = false;
    }

    VoicePlayback_ClampPosPastEndWhenGateOff(setup.end,
                                             st.old_gate,
                                             st.old_pos_frame,
                                             st.old_pos_frac);

    st.steal_fade_level -= setup.steal_fade_step;
    if(st.steal_fade_level <= 0.0f)
    {
        CompleteStealFadeOut_(v);
        return true;
    }
    return false;
}

void VoiceEngine::RenderStealFadeOutVoice_(Voice& v,
                                           const RenderVoiceContext& ctx,
                                           float pitch_scale,
                                           StopFadeState& stop_fade)
{
    if(v.sample == nullptr || v.sample->pcm == nullptr || v.sample->length == 0)
    {
        v.state = VoiceState::Idle;
        ClearPolyPortoVoice_(v);
        return;
    }

    EffectivePlaybackRegion r{};
    bool old_loop_enabled = false;
    ResolveEffectivePlaybackRegion_(v, ctx, r, old_loop_enabled);
    RenderStealFadeOutSetup setup{};
    setup.start = r.start;
    setup.end = r.end;
    setup.ls_i = r.ls_i;
    setup.le_i = r.le_i;
    setup.use_edit = r.use_edit;
    setup.edit_gain = r.edit_gain;
    setup.old_layer = v.old_source_layer & 1u;
    setup.old_loop_enabled = old_loop_enabled;
    setup.loop_voice = v.loop_voice;
    if(setup.loop_voice)
    {
        setup.old_loop_enabled = true;
        setup.ls_i = setup.start;
        setup.le_i = setup.end;
    }
    setup.length_f = static_cast<float>(setup.end);
    setup.ls       = static_cast<float>(setup.ls_i);
    setup.le       = static_cast<float>(setup.le_i);
    RenderStealFadeOutLoopState st{};
    st.old_pos_frame    = v.old_pos_frame;
    st.old_pos_frac     = v.old_pos_frac;
    st.old_gate         = v.old_gate;
    st.old_dir          = v.old_dir;
    st.steal_fade_level = v.steal_fade_level;
    st.stop_fade        = stop_fade;

    VoicePlayback_NormalizeStealFadeOutPositions(setup.end,
                                                 setup.ls_i,
                                                 setup.start,
                                                 setup.old_loop_enabled,
                                                 st.old_pos_frame,
                                                 st.old_pos_frac,
                                                 st.old_gate);

    const bool old_preview_sample
        = (r.slot == static_cast<int>(kRecordPreviewSampleIndex));
    const float old_pitch_ratio_scale
        = old_preview_sample ? 1.0f : (ctx.engine_tune_scale[setup.old_layer] * pitch_scale);
    setup.old_ratio = v.old_ratio * old_pitch_ratio_scale;
    setup.old_gain  = v.old_gain * setup.edit_gain * ctx.engine_voice_gain[setup.old_layer];
    setup.old_seam_frames
        = setup.loop_voice ? ComputeLoopSeamCrossfadeFrames(setup.start,
                                                            setup.end,
                                                            loop_crossfade_amount_[setup.old_layer])
                           : 0u;
    setup.old_crossfade_seam_frames
        = layer_seam_baked_[setup.old_layer] ? 0u : setup.old_seam_frames;
    setup.old_loop_mode = setup.loop_voice ? LoopMode::Forward : ctx.loop_mode;
    setup.steal_fade_step = v.steal_fade_step;
    {
        float ff = sample_rate_ * 0.001f * kLoopBoundaryFadeMs;
        if(ff < 1.0f)
            ff = 1.0f;
        if(setup.end > setup.start)
        {
            const float region = static_cast<float>(setup.end - setup.start);
            if(ff > region * 0.5f)
                ff = region * 0.5f;
        }
        else
        {
            ff = 0.0f;
        }
        if(ff < 0.0f)
            ff = 0.0f;
        setup.loop_fade_frames          = ff;
        setup.loop_fade_start_threshold = static_cast<float>(setup.start) + ff;
        setup.loop_fade_end_threshold   = static_cast<float>(setup.end) - ff;
    }

    if(st.stop_fade.active)
        st.old_gate = false;

    for(size_t i = 0; i < ctx.size; i++)
    {
        if(RenderStealFadeOut_ProcessOneSample_(v, ctx, setup, i, st))
            break;
    }

    if(v.state == VoiceState::Idle)
        return;

    if(v.state == VoiceState::StealFadeOut)
    {
        v.old_pos_frame    = st.old_pos_frame;
        v.old_pos_frac     = st.old_pos_frac;
        v.old_gate         = st.old_gate;
        v.old_dir          = st.old_dir;
        v.steal_fade_level = st.steal_fade_level;
        v.stop_fade_active = st.stop_fade.active;
        v.stop_fade_samples_remaining = st.stop_fade.remaining;
        v.stop_fade_level  = st.stop_fade.level;
        v.stop_fade_step   = st.stop_fade.step;
    }

    stop_fade = st.stop_fade;

    if(v.state == VoiceState::StealFadeOut)
    {
        VoiceRender_PlayheadMetricIfAudible_(v,
                                             ctx,
                                             v.old_source_layer & 1u,
                                             st.old_pos_frame,
                                             st.old_pos_frac,
                                             st.steal_fade_level);
    }
}

bool VoiceEngine::RenderNormalVoice_ProcessOneSample_(Voice& v,
                                                      const RenderVoiceContext& ctx,
                                                      const RenderNormalVoicePerBlockSetup& setup,
                                                      size_t i,
                                                      RenderNormalVoiceLoopState& st)
{
    uint32_t fetch_start = 0u;
    if(ctx.fetch_cycles)
        fetch_start = DWT->CYCCNT;

    const float ratio = st.ratio * setup.pitch_ratio_scale;
    bool used_seam_xfade = false;
    float s = VoiceRenderFetch_VoiceStream(v.sample,
                                           st.pos_frame,
                                           st.pos_frac,
                                           setup.gain,
                                           setup.loop_voice,
                                           setup.start,
                                           setup.end,
                                           setup.crossfade_seam_frames,
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
                                                st.pos_frame,
                                                st.pos_frac,
                                                setup.loop_fade_start_threshold,
                                                setup.loop_fade_end_threshold,
                                                setup.start,
                                                setup.end,
                                                sample_rate_);
    if(ctx.fetch_cycles)
        *ctx.fetch_cycles += DWT->CYCCNT - fetch_start;

    uint32_t envmix_start = 0u;
    if(ctx.envmix_cycles)
        envmix_start = DWT->CYCCNT;

    s *= st.env_level;
    const float fin = VoicePlayback_FadeInMultiplier(st.fade);
    s *= fin;
    if(st.stop_fade.active)
        s *= st.stop_fade.level;
    float* layer_bus = (setup.source_layer == 0u) ? ctx.outL : ctx.outR;
    layer_bus[i] += s;

    if(StopFade_AdvanceAndFinishIfDone_(v, st.stop_fade))
    {
        if(ctx.envmix_cycles)
            *ctx.envmix_cycles += DWT->CYCCNT - envmix_start;
        return true;
    }

    if(!AdvancePos(st.pos_frame,
                   st.pos_frac,
                   st.dir,
                   ratio,
                   setup.end,
                   setup.ls_i,
                   setup.le_i,
                   setup.loop_enabled,
                   st.gate,
                   setup.voice_loop_mode,
                   setup.loop_voice ? setup.seam_frames : 0u))
    {
        const bool need_stop_fade = !st.stop_fade.active;
        BeginStopFadeOnStreamEnd_(v, st.stop_fade);
        if(need_stop_fade)
            st.gate = false;
        VoicePlayback_ClampPosToLastFrameIfValid(setup.end,
                                                 st.pos_frame,
                                                 st.pos_frac);
    }
    if(st.fade < 1.0f)
        VoicePlayback_StepFadeIn(st.fade, setup.fade_step);

    if(setup.block_rate_env)
    {
        // P5: envelope was simulated once per block. Per-sample we just
        // linearly ramp env_level toward env_end_level. The state machine
        // transition (including ending on Off) is finalized after the
        // per-sample loop in RenderNormalVoice_.
        st.env_level += setup.env_per_sample_delta;
    }
    else
    {
        StepEnvelope(st.env_stage,
                     st.env_level,
                     setup.env_a_step,
                     setup.env_d_step,
                     setup.env_sustain,
                     st.env_r_step);
        if(st.env_stage == EnvStage::Off && !st.stop_fade.active)
        {
            v.state = VoiceState::Idle;
            ClearPolyPortoVoice_(v);
            if(ctx.envmix_cycles)
                *ctx.envmix_cycles += DWT->CYCCNT - envmix_start;
            return true;
        }
    }

    if(st.glide_active)
    {
        if(st.glide_samples_remaining > 0u)
        {
            --st.glide_samples_remaining;
            if(st.glide_samples_remaining == 0u)
            {
                st.glide_current_semitones = st.glide_target_semitones;
                st.ratio = ComputeRatioFromSemitoneDelta(st.glide_target_semitones);
                st.glide_step_semitones = 0.0f;
                st.glide_active = false;
            }
            else
            {
                st.glide_current_semitones += st.glide_step_semitones;
                st.ratio = ComputeRatioFromSemitoneDelta(st.glide_current_semitones);
            }
        }
        else
        {
            st.glide_active = false;
            st.glide_step_semitones = 0.0f;
            st.ratio = ComputeRatioFromSemitoneDelta(st.glide_target_semitones);
        }
    }
    if(ctx.envmix_cycles)
        *ctx.envmix_cycles += DWT->CYCCNT - envmix_start;
    return false;
}

void VoiceEngine::RenderNormalVoice_Batched_(Voice& v,
                                             const RenderVoiceContext& ctx,
                                             const RenderNormalVoicePerBlockSetup& setup,
                                             RenderNormalVoiceLoopState& st)
{
    // Max audio block size this firmware ever uses is 48 (SetAudioBlockSize in
    // main.cpp). We keep the stack buffer right-sized; any larger block falls
    // back to the per-sample path to stay off the heap and avoid stack
    // overruns on the audio thread.
    constexpr size_t kMaxBatch = 48;
    const size_t N = ctx.size;
    if(N == 0)
        return;
    if(N > kMaxBatch)
    {
        for(size_t i = 0; i < N; ++i)
        {
            if(RenderNormalVoice_ProcessOneSample_(v, ctx, setup, i, st))
                return;
        }
        return;
    }

    float buf[kMaxBatch];

    VoiceBatchFetchParams p;
    p.sample               = v.sample;
    p.start                = setup.start;
    p.end                  = setup.end;
    p.ls_i                 = setup.ls_i;
    p.le_i                 = setup.le_i;
    p.length_f             = setup.length_f;
    p.ls                   = setup.ls;
    p.le                   = setup.le;
    p.use_edit             = setup.use_edit;
    p.region_loop_enabled  = setup.loop_enabled;
    p.loop_enabled         = setup.loop_enabled;
    p.voice_loop_mode      = setup.voice_loop_mode;
    p.layer_loop_voice     = setup.loop_voice;
    p.seam_frames          = setup.seam_frames;
    p.crossfade_seam_frames = setup.crossfade_seam_frames;
    p.loop_shape           = loop_crossfade_shape_[setup.source_layer];
    p.sample_rate          = sample_rate_;
    p.ratio                = setup.ratio * setup.pitch_ratio_scale;
    p.gain                 = setup.gain;
    p.fade_start_threshold = setup.loop_fade_start_threshold;
    p.fade_end_threshold   = setup.loop_fade_end_threshold;

    // Phase 1: fetch + boundary-fade + pos advance for the whole batch.
    uint32_t fetch_start = 0u;
    if(ctx.fetch_cycles)
        fetch_start = DWT->CYCCNT;
    const size_t eos_idx = VoiceRenderFetch_VoiceStreamBatch(p,
                                                             st.pos_frame,
                                                             st.pos_frac,
                                                             st.dir,
                                                             st.gate,
                                                             N,
                                                             buf,
                                                             ctx.fetch_seam_cycles,
                                                             ctx.fetch_seam_count);
    if(ctx.fetch_cycles)
        *ctx.fetch_cycles += DWT->CYCCNT - fetch_start;

    // Phase 2: ramp + mix. Matches the per-sample order of operations in
    // RenderNormalVoice_ProcessOneSample_:
    //   1. Write layer_bus[i] += buf[i] * (env * fade_in * sf_level)
    //   2. Advance stop-fade (may finalize the voice mid-block)
    //   3. Activate stop-fade iff i == eos_idx (takes effect next sample)
    //   4. Advance fade_in
    //   5. Advance env (linear, block_rate_env)
    float* layer_bus = (setup.source_layer == 0u) ? ctx.outL : ctx.outR;

    float env = st.env_level;
    const float env_delta = setup.env_per_sample_delta;

    float fade = st.fade;
    const float fade_step = setup.fade_step;
    bool fade_saturated = !(fade < 1.0f);

    bool    sf_active    = st.stop_fade.active;
    float   sf_level     = st.stop_fade.level;
    float   sf_step      = st.stop_fade.step;
    int32_t sf_remaining = st.stop_fade.remaining;

    uint32_t envmix_start = 0u;
    if(ctx.envmix_cycles)
        envmix_start = DWT->CYCCNT;
    for(size_t i = 0; i < N; ++i)
    {
        const float fin = fade_saturated ? 1.0f : fade;
        float m = env * fin;
        if(sf_active)
            m *= sf_level;
        layer_bus[i] += buf[i] * m;

        if(sf_active)
        {
            sf_remaining--;
            sf_level -= sf_step;
            if(sf_level < 0.0f)
                sf_level = 0.0f;
            if(sf_remaining <= 0)
            {
                // Stop-fade completed. FinishStopFade_ clears Voice fields and
                // sets v.state = Idle, so the caller skips the commit path.
                st.stop_fade.active    = false;
                st.stop_fade.remaining = 0;
                st.stop_fade.level     = 0.0f;
                st.stop_fade.step      = sf_step;
                FinishStopFade_(v);
                if(ctx.envmix_cycles)
                    *ctx.envmix_cycles += DWT->CYCCNT - envmix_start;
                return;
            }
        }

        // End-of-stream handoff: sample i was already written with the
        // pre-eos sf state. Activate stop-fade now so sample i+1 onward uses
        // the fresh sf ramp (sf_level=1.0, sf_remaining=fade_samples).
        if(i == eos_idx && !sf_active)
        {
            BeginStopFadeOnStreamEnd_(v, st.stop_fade);
            sf_active    = st.stop_fade.active;
            sf_level     = st.stop_fade.level;
            sf_step      = st.stop_fade.step;
            sf_remaining = st.stop_fade.remaining;
        }

        if(!fade_saturated)
        {
            fade += fade_step;
            if(fade >= 1.0f)
            {
                fade           = 1.0f;
                fade_saturated = true;
            }
        }

        env += env_delta;
    }
    if(ctx.envmix_cycles)
        *ctx.envmix_cycles += DWT->CYCCNT - envmix_start;

    // Commit local ramp state. pos/dir/gate were already committed in-place
    // by VoiceRenderFetch_VoiceStreamBatch. env_stage is finalized by the
    // caller from the block-start pre-simulation.
    st.env_level           = env;
    st.fade                = fade;
    st.stop_fade.active    = sf_active;
    st.stop_fade.level     = sf_level;
    st.stop_fade.step      = sf_step;
    st.stop_fade.remaining = sf_remaining;
}

void VoiceEngine::RenderNormalVoice_BatchedFastEnv_(Voice& v,
                                                    const RenderVoiceContext& ctx,
                                                    const RenderNormalVoicePerBlockSetup& setup,
                                                    RenderNormalVoiceLoopState& st)
{
    constexpr size_t kMaxBatch = 48;
    const size_t N = ctx.size;
    if(N == 0)
        return;
    if(N > kMaxBatch)
    {
        for(size_t i = 0; i < N; ++i)
            if(RenderNormalVoice_ProcessOneSample_(v, ctx, setup, i, st))
                return;
        return;
    }

    float buf[kMaxBatch];

    VoiceBatchFetchParams p;
    p.sample               = v.sample;
    p.start                = setup.start;
    p.end                  = setup.end;
    p.ls_i                 = setup.ls_i;
    p.le_i                 = setup.le_i;
    p.length_f             = setup.length_f;
    p.ls                   = setup.ls;
    p.le                   = setup.le;
    p.use_edit             = setup.use_edit;
    p.region_loop_enabled  = setup.loop_enabled;
    p.loop_enabled         = setup.loop_enabled;
    p.voice_loop_mode      = setup.voice_loop_mode;
    p.layer_loop_voice     = setup.loop_voice;
    p.seam_frames          = setup.seam_frames;
    p.crossfade_seam_frames = setup.crossfade_seam_frames;
    p.loop_shape           = loop_crossfade_shape_[setup.source_layer];
    p.sample_rate          = sample_rate_;
    p.ratio                = setup.ratio * setup.pitch_ratio_scale;
    p.gain                 = setup.gain;
    p.fade_start_threshold = setup.loop_fade_start_threshold;
    p.fade_end_threshold   = setup.loop_fade_end_threshold;

    // Phase 1: identical optimized batch fetch as the slow path.
    uint32_t fetch_start = 0u;
    if(ctx.fetch_cycles)
        fetch_start = DWT->CYCCNT;
    const size_t eos_idx = VoiceRenderFetch_VoiceStreamBatch(p,
                                                             st.pos_frame,
                                                             st.pos_frac,
                                                             st.dir,
                                                             st.gate,
                                                             N,
                                                             buf,
                                                             ctx.fetch_seam_cycles,
                                                             ctx.fetch_seam_count);
    if(ctx.fetch_cycles)
        *ctx.fetch_cycles += DWT->CYCCNT - fetch_start;

    // Phase 2: per-sample envelope state machine + fade + stop-fade + mix. Order
    // of operations matches RenderNormalVoice_ProcessOneSample_ exactly:
    //   1. mix with the current env_level
    //   2. advance stop-fade (may finalize the voice)
    //   3. end-of-stream handoff at eos_idx
    //   4. advance fade-in
    //   5. step the envelope (inlined); Off (with no stop-fade) finalizes Idle
    float* layer_bus = (setup.source_layer == 0u) ? ctx.outL : ctx.outR;

    EnvStage    env_stage = st.env_stage;
    float       env       = st.env_level;
    const float a_step    = setup.env_a_step;
    const float d_step    = setup.env_d_step;
    const float sustain   = setup.env_sustain;
    const float r_step    = st.env_r_step;

    float       fade           = st.fade;
    const float fade_step      = setup.fade_step;
    bool        fade_saturated = !(fade < 1.0f);

    bool    sf_active    = st.stop_fade.active;
    float   sf_level     = st.stop_fade.level;
    float   sf_step      = st.stop_fade.step;
    int32_t sf_remaining = st.stop_fade.remaining;

    uint32_t envmix_start = 0u;
    if(ctx.envmix_cycles)
        envmix_start = DWT->CYCCNT;
    for(size_t i = 0; i < N; ++i)
    {
        const float fin = fade_saturated ? 1.0f : fade;
        float m = env * fin;
        if(sf_active)
            m *= sf_level;
        layer_bus[i] += buf[i] * m;

        if(sf_active)
        {
            sf_remaining--;
            sf_level -= sf_step;
            if(sf_level < 0.0f)
                sf_level = 0.0f;
            if(sf_remaining <= 0)
            {
                st.stop_fade.active    = false;
                st.stop_fade.remaining = 0;
                st.stop_fade.level     = 0.0f;
                st.stop_fade.step      = sf_step;
                FinishStopFade_(v);
                if(ctx.envmix_cycles)
                    *ctx.envmix_cycles += DWT->CYCCNT - envmix_start;
                return;
            }
        }

        if(i == eos_idx && !sf_active)
        {
            BeginStopFadeOnStreamEnd_(v, st.stop_fade);
            sf_active    = st.stop_fade.active;
            sf_level     = st.stop_fade.level;
            sf_step      = st.stop_fade.step;
            sf_remaining = st.stop_fade.remaining;
        }

        if(!fade_saturated)
        {
            fade += fade_step;
            if(fade >= 1.0f)
            {
                fade           = 1.0f;
                fade_saturated = true;
            }
        }

        // Inlined StepEnvelope (mirrors voice_engine_playback.cpp).
        switch(env_stage)
        {
            case EnvStage::Attack:
                env += a_step;
                if(env >= 1.0f)
                {
                    env       = 1.0f;
                    env_stage = EnvStage::Decay;
                }
                break;
            case EnvStage::Decay:
                env -= d_step;
                if(env <= sustain)
                {
                    env       = sustain;
                    env_stage = EnvStage::Sustain;
                }
                break;
            case EnvStage::Sustain:
                env = sustain;
                break;
            case EnvStage::Release:
                env -= r_step;
                if(env <= 0.0f)
                {
                    env       = 0.0f;
                    env_stage = EnvStage::Off;
                }
                break;
            case EnvStage::Off:
            default:
                env = 0.0f;
                break;
        }
        if(env_stage == EnvStage::Off && !sf_active)
        {
            // Envelope finished: voice goes Idle. Remaining samples are silent;
            // pos was advanced by the batch fetch but is not committed (Idle).
            v.state = VoiceState::Idle;
            ClearPolyPortoVoice_(v);
            if(ctx.envmix_cycles)
                *ctx.envmix_cycles += DWT->CYCCNT - envmix_start;
            return;
        }
    }
    if(ctx.envmix_cycles)
        *ctx.envmix_cycles += DWT->CYCCNT - envmix_start;

    st.env_level           = env;
    st.env_stage           = env_stage;
    st.fade                = fade;
    st.stop_fade.active    = sf_active;
    st.stop_fade.level     = sf_level;
    st.stop_fade.step      = sf_step;
    st.stop_fade.remaining = sf_remaining;
}

void VoiceEngine::RenderNormalVoice_(Voice& v,
                                     const RenderVoiceContext& ctx,
                                     float pitch_scale,
                                     StopFadeState& stop_fade)
{
    if(v.sample == nullptr || v.sample->pcm == nullptr || v.sample->length == 0)
    {
        v.state = VoiceState::Idle;
        ClearPolyPortoVoice_(v);
        return;
    }

    uint32_t setup_start = 0u;
    if(ctx.setup_cycles)
        setup_start = DWT->CYCCNT;

    const uint8_t source_layer = v.source_layer & 1u;
    const bool loop_voice = v.loop_voice;

    // Per-voice cache of the block-constant portion. For a held voice with no
    // intervening UI param change, every field below is identical block-to-block;
    // skip the recompute entirely.
    VoiceBlockSetupCache& cache = voice_setup_cache_[&v - voices_];
    const bool cache_hit = cache.valid
                           && cache.gen_seen == setup_cache_gen_
                           && cache.sample == v.sample
                           && cache.loop_voice == loop_voice
                           && cache.source_layer == source_layer;
    if(!cache_hit)
    {
        EffectivePlaybackRegion r{};
        bool loop_enabled = false;
        ResolveEffectivePlaybackRegion_(v, ctx, r, loop_enabled);
        cache.start = r.start;
        cache.end = r.end;
        cache.ls_i = r.ls_i;
        cache.le_i = r.le_i;
        cache.use_edit = r.use_edit;
        cache.edit_gain = r.edit_gain;
        cache.slot = r.slot;
        cache.loop_enabled = loop_enabled;
        if(loop_voice)
        {
            cache.loop_enabled = true;
            cache.ls_i = cache.start;
            cache.le_i = cache.end;
        }
        cache.length_f = static_cast<float>(cache.end);
        cache.ls = static_cast<float>(cache.ls_i);
        cache.le = static_cast<float>(cache.le_i);
        cache.seam_frames
            = loop_voice
                  ? ComputeLoopSeamCrossfadeFrames(
                        cache.start, cache.end, loop_crossfade_amount_[source_layer])
                  : 0u;
        cache.crossfade_seam_frames
            = layer_seam_baked_[source_layer] ? 0u : cache.seam_frames;
        cache.voice_loop_mode = loop_voice ? LoopMode::Forward : ctx.loop_mode;
        cache.preview_sample
            = (cache.slot == static_cast<int>(kRecordPreviewSampleIndex));
        // Loop-boundary fade thresholds.
        {
            float ff = sample_rate_ * 0.001f * kLoopBoundaryFadeMs;
            if(ff < 1.0f)
                ff = 1.0f;
            if(cache.end > cache.start)
            {
                const float region = static_cast<float>(cache.end - cache.start);
                if(ff > region * 0.5f)
                    ff = region * 0.5f;
            }
            else
            {
                ff = 0.0f;
            }
            if(ff < 0.0f)
                ff = 0.0f;
            cache.loop_fade_frames          = ff;
            cache.loop_fade_start_threshold = static_cast<float>(cache.start) + ff;
            cache.loop_fade_end_threshold   = static_cast<float>(cache.end) - ff;
        }
        cache.sample       = v.sample;
        cache.source_layer = source_layer;
        cache.loop_voice   = loop_voice;
        cache.gen_seen     = setup_cache_gen_;
        cache.valid        = true;
    }

    RenderNormalVoicePerBlockSetup setup{};
    setup.start                 = cache.start;
    setup.end                   = cache.end;
    setup.length_f              = cache.length_f;
    setup.ls                    = cache.ls;
    setup.le                    = cache.le;
    setup.ls_i                  = cache.ls_i;
    setup.le_i                  = cache.le_i;
    setup.loop_enabled          = cache.loop_enabled;
    setup.loop_voice            = loop_voice;
    setup.seam_frames           = cache.seam_frames;
    setup.crossfade_seam_frames = cache.crossfade_seam_frames;
    setup.voice_loop_mode       = cache.voice_loop_mode;
    setup.source_layer          = source_layer;
    setup.use_edit              = cache.use_edit;
    // Per-voice/per-block values (not cached — depend on v.* or per-block scalars).
    setup.gain  = v.gain * cache.edit_gain * ctx.engine_voice_gain[source_layer];
    setup.ratio = v.ratio;
    setup.pitch_ratio_scale
        = cache.preview_sample ? 1.0f : (ctx.engine_tune_scale[source_layer] * pitch_scale);
    setup.fade_step   = v.fade_in_step;
    setup.env_a_step  = v.env_a_step;
    setup.env_d_step  = v.env_d_step;
    setup.env_sustain = v.env_sustain;
    setup.loop_fade_frames          = cache.loop_fade_frames;
    setup.loop_fade_start_threshold = cache.loop_fade_start_threshold;
    setup.loop_fade_end_threshold   = cache.loop_fade_end_threshold;

    RenderNormalVoiceLoopState st{};
    st.pos_frame = v.pos_frame;
    st.pos_frac = v.pos_frac;
    st.gate = v.gate;
    st.dir = v.dir;
    st.fade = v.fade_in;
    st.env_stage = v.env_stage;
    st.env_level = v.env_level;
    st.env_r_step = v.env_r_step;
    st.ratio = v.ratio;
    st.glide_active = v.poly_porto_glide_active;
    st.glide_current_semitones = v.poly_porto_current_semitones;
    st.glide_target_semitones = v.poly_porto_target_semitones;
    st.glide_step_semitones = v.poly_porto_step_semitones;
    st.glide_samples_remaining = v.poly_porto_slide_samples_remaining;
    st.stop_fade = stop_fade;
    if(st.stop_fade.active)
        st.gate = false;
    VoicePlayback_NormalizeVoiceBlockStart(setup.end,
                                           setup.ls_i,
                                           setup.start,
                                           setup.loop_enabled,
                                           st.gate,
                                           st.pos_frame,
                                           st.pos_frac);

    // P5: decide whether this voice can use block-rate envelope simulation
    // for this block. Only "slow" voices (all stages >= 5 ms) qualify; any
    // stage faster than the threshold keeps the per-sample state machine.
    // Fast-path is scoped to RenderNormalVoice_; StealFadeOut keeps per-sample.
    setup.block_rate_env       = !st.glide_active
                              && (setup.env_a_step <= kFastEnvelopeStepThreshold)
                              && (setup.env_d_step <= kFastEnvelopeStepThreshold)
                              && (st.env_r_step    <= kFastEnvelopeStepThreshold);
    setup.env_per_sample_delta = 0.0f;

    if(ctx.setup_cycles)
        *ctx.setup_cycles += DWT->CYCCNT - setup_start;

    uint32_t presim_start = 0u;
    if(ctx.presim_cycles)
        presim_start = DWT->CYCCNT;

    EnvStage env_end_stage      = st.env_stage;
    float    env_end_level      = st.env_level;
    bool     env_ended_in_block = false;
    if(setup.block_rate_env)
    {
        if(st.env_stage == EnvStage::Sustain)
        {
            // Sustain is a fixed point of StepEnvelope: it pins level to
            // env_sustain and never leaves the stage. Skip the per-sample
            // pre-sim entirely and ramp directly to (a possibly updated)
            // sustain level over the block. Exact match for the loop result.
            env_end_stage = EnvStage::Sustain;
            env_end_level = setup.env_sustain;
        }
        else
        {
            // Simulate ctx.size iterations of the state machine locally so we
            // know the end-of-block stage/level. Inside the per-sample loop we
            // linearly ramp env_level from its current value to env_end_level;
            // stage stays pinned to its current value for per-sample branches
            // and is committed at block end.
            EnvStage sim_stage  = st.env_stage;
            float    sim_level  = st.env_level;
            const float sim_r_step = st.env_r_step;
            for(size_t k = 0; k < ctx.size; ++k)
            {
                StepEnvelope(sim_stage,
                             sim_level,
                             setup.env_a_step,
                             setup.env_d_step,
                             setup.env_sustain,
                             sim_r_step);
                if(sim_stage == EnvStage::Off)
                {
                    env_ended_in_block = true;
                    break;
                }
            }
            env_end_stage = sim_stage;
            env_end_level = sim_level;
        }
        if(ctx.size > 0)
            setup.env_per_sample_delta
                = (env_end_level - st.env_level) / static_cast<float>(ctx.size);
    }

    if(ctx.presim_cycles)
        *ctx.presim_cycles += DWT->CYCCNT - presim_start;

    if(setup.block_rate_env)
    {
        // P2: batched fetch + ramp/mix pipeline. Env_level is linearly ramped
        // across the block (pre-simulated above); stage transitions do not
        // occur mid-block on this path, so fetch is decoupled from the ramp.
        RenderNormalVoice_Batched_(v, ctx, setup, st);
    }
    else if(!st.glide_active)
    {
        // Fast-envelope path: same optimized batch fetch as the slow path, with
        // a per-sample StepEnvelope so mid-block stage transitions (incl. Off)
        // are sample-accurate. This replaces the old per-sample fetch loop,
        // whose cross-TU calls overran the CPU and caused buffer-underrun noise.
        RenderNormalVoice_BatchedFastEnv_(v, ctx, setup, st);
    }
    else
    {
        // Gliding (poly-porto) voices: ratio changes per sample, which the
        // fixed-ratio batch fetch cannot represent, so keep the per-sample path.
        for(size_t i = 0; i < ctx.size; i++)
        {
            if(RenderNormalVoice_ProcessOneSample_(v, ctx, setup, i, st))
                break;
        }
    }

    // P5: block-rate finalize. The per-sample loop only ramped env_level; the
    // authoritative stage + exact end level come from the block-start
    // simulation. If the envelope fully completed within the block, mark the
    // voice Idle so commit/playhead tracking are skipped below (matching the
    // per-sample path's early-return behavior).
    if(setup.block_rate_env && v.state != VoiceState::Idle)
    {
        st.env_level = env_end_level;
        st.env_stage = env_end_stage;
        if(env_ended_in_block && !st.stop_fade.active)
        {
            v.state = VoiceState::Idle;
            ClearPolyPortoVoice_(v);
        }
    }

    stop_fade = st.stop_fade;

    if(v.state != VoiceState::Idle)
    {
        CommitNormalVoiceLoopState_(v, st);

        VoiceRender_PlayheadMetricIfAudible_(v,
                                             ctx,
                                             source_layer & 1u,
                                             st.pos_frame,
                                             st.pos_frac,
                                             st.env_level);
    }
}
