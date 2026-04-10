#include "voice_engine_internal.h"

#include "build_config.h"

#include <cmath>
#include <cstring>

static constexpr float kReleaseTauSec      = 0.030f;
static constexpr float kPitchModSemitones  = 2.0f;
static constexpr float kMacroSmoothSec     = 0.005f;
static constexpr float kEngineTuneMinSemitones = -48.0f;
static constexpr float kEngineTuneMaxSemitones = 48.0f;

void VoiceEngine::SnapshotMacroState_()
{
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
}

void VoiceEngine::SnapshotPLockState_()
{
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
}

void VoiceEngine::SnapshotRenderEditState_(SampleEdit& edit, const Sample*& edit_sample) const
{
    edit = current_edit_;
    edit_sample = edit_sample_;
}

const ModRoute* VoiceEngine::SnapshotModRoutes_(ModRoute (&routes_local)[kMaxModRoutes]) const
{
    if(!mod_matrix_)
        return nullptr;

    const uint8_t sel = mod_matrix_->routes_sel.load(std::memory_order_acquire) & 1u;
    const ModRoute* routes = (sel == 0) ? mod_matrix_->routes_a : mod_matrix_->routes_b;
    for(size_t ri = 0; ri < kMaxModRoutes; ++ri)
        routes_local[ri] = routes[ri];
    return routes_local;
}

void VoiceEngine::PrepareRenderScalars_(float (&engine_tune_scale)[kEngineLayerCount],
                                        float (&engine_voice_gain)[kEngineLayerCount],
                                        float& lfo_depth,
                                        float& env_amount) const
{
    for(uint8_t layer = 0; layer < kEngineLayerCount; ++layer)
    {
        float tune = engine_tune_semitones_[layer];
        if(tune < kEngineTuneMinSemitones)
            tune = kEngineTuneMinSemitones;
        if(tune > kEngineTuneMaxSemitones)
            tune = kEngineTuneMaxSemitones;
        engine_tune_scale[layer] = std::pow(2.0f, tune / 12.0f);

        float gain = engine_layer_scale_[layer];
        if(gain < 0.0f)
            gain = 0.0f;
        engine_voice_gain[layer] = gain;
    }

    lfo_depth = lfo_depth_;
    env_amount = env_amount_;
}

void VoiceEngine::RefreshBlockState_(size_t size)
{
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
}

void VoiceEngine::WriteRenderDebug_(uint32_t clip_block,
                                    float rate_hz,
                                    float depth,
                                    float lfo_src,
                                    float max_env,
                                    uint32_t active,
                                    const uint32_t (&playhead_frame)[2],
                                    const uint32_t (&playhead_active)[2])
{
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
    for(uint8_t layer = 0; layer < 2; ++layer)
    {
        if(playhead_frame_out_[layer])
            playhead_frame_out_[layer]->store(playhead_frame[layer], std::memory_order_relaxed);
        if(playhead_active_out_[layer])
            playhead_active_out_[layer]->store(playhead_active[layer], std::memory_order_relaxed);
    }
    active_last_block_ = active;
}

struct VoiceEngine::RenderVoiceContext
{
    float* outL;
    float* outR;
    size_t size;
    LoopMode loop_mode;
    SampleEdit edit;
    const Sample* edit_sample;
    const float* engine_tune_scale;
    const float* engine_voice_gain;
    uint32_t* playhead_frame;
    uint32_t* playhead_active;
    float* playhead_metric;
};

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
    if(old_loop_enabled && old_pos >= length_f)
        old_pos = ls + (old_pos - length_f);
    if(new_loop_enabled && new_pos >= length_f)
        new_pos = ls + (new_pos - length_f);
    if(old_pos < static_cast<float>(start))
        old_pos = static_cast<float>(start);
    if(new_pos < static_cast<float>(start))
        new_pos = static_cast<float>(start);
    if(!old_loop_enabled && old_pos >= length_f && length_f > 0.0f)
    {
        old_gate = false;
        old_pos = length_f - 1.0f;
    }
    if(!new_loop_enabled && new_pos >= length_f && length_f > 0.0f)
    {
        new_gate = false;
        new_pos = length_f - 1.0f;
    }

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
        float x_clamped = x;
        if(x_clamped > 1.0f)
            x_clamped = 1.0f;

        float s_old = 0.0f;
        float s_new = 0.0f;
        bool old_used_seam_xfade = false;
        bool new_used_seam_xfade = false;
        if(v.loop_voice)
        {
            s_old = SampleAtLoopSeamCrossfade(v.sample,
                                              old_pos,
                                              start,
                                              end,
                                              old_seam_frames,
                                              loop_crossfade_shape_[old_layer],
                                              sample_rate_,
                                              old_used_seam_xfade)
                    * old_gain;
        }
        else if(use_edit)
        {
            s_old = SampleAtLinearRegion(v.sample,
                                         old_pos,
                                         start,
                                         end,
                                         old_loop_enabled,
                                         ls_i,
                                         le_i) * old_gain;
        }
        else
        {
            const bool old_wrap = (old_loop_enabled && old_gate
                                   && v.sample->loop_start == 0
                                   && v.sample->loop_end == v.sample->length);
            s_old = SampleAtLinear(v.sample, old_pos, old_wrap) * old_gain;
        }

        if(v.new_loop_voice)
        {
            s_new = SampleAtLoopSeamCrossfade(v.sample,
                                              new_pos,
                                              start,
                                              end,
                                              new_seam_frames,
                                              loop_crossfade_shape_[new_layer],
                                              sample_rate_,
                                              new_used_seam_xfade)
                    * new_gain;
        }
        else if(use_edit)
        {
            s_new = SampleAtLinearRegion(v.sample,
                                         new_pos,
                                         start,
                                         end,
                                         new_loop_enabled,
                                         ls_i,
                                         le_i) * new_gain;
        }
        else
        {
            const bool new_wrap = (new_loop_enabled && new_gate
                                   && v.sample->loop_start == 0
                                   && v.sample->loop_end == v.sample->length);
            s_new = SampleAtLinear(v.sample, new_pos, new_wrap) * new_gain;
        }
        if(v.loop_voice && old_seam_frames == 0u && !old_used_seam_xfade)
            s_old *= ComputeLoopBoundaryFade(old_pos, start, end, sample_rate_);
        if(v.new_loop_voice && new_seam_frames == 0u && !new_used_seam_xfade)
            s_new *= ComputeLoopBoundaryFade(new_pos, start, end, sample_rate_);
        s_new *= new_env_level;
        const float fin_new = (new_fade < 1.0f) ? new_fade : 1.0f;
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
            float p = v.new_pos;
            if(p < 0.0f)
                p = 0.0f;
            const float pmax = static_cast<float>(v.sample->length - 1);
            if(p > pmax)
                p = pmax;
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
    float* layer_bus = (source_layer == 0u) ? ctx.outL : ctx.outR;

    for(size_t i = 0; i < ctx.size; i++)
    {
        float s = 0.0f;
        bool used_seam_xfade = false;
        if(loop_voice)
        {
            s = SampleAtLoopSeamCrossfade(v.sample,
                                          pos,
                                          start,
                                          end,
                                          seam_frames,
                                          loop_crossfade_shape_[source_layer],
                                          sample_rate_,
                                          used_seam_xfade)
                * gain;
        }
        else if(use_edit)
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
        if(loop_voice && seam_frames == 0u && !used_seam_xfade)
            s *= ComputeLoopBoundaryFade(pos, start, end, sample_rate_);
        s *= env_level;
        const float fin = (fade < 1.0f) ? fade : 1.0f;
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
                float p = pos;
                if(p < 0.0f)
                    p = 0.0f;
                const float pmax = static_cast<float>(v.sample->length - 1);
                if(p > pmax)
                    p = pmax;
                ctx.playhead_frame[ui_layer] = static_cast<uint32_t>(p);
                ctx.playhead_active[ui_layer] = 1u;
            }
        }
    }
}

void VoiceEngine::RenderBlock(float* outL, float* outR, size_t size)
{
    if(!outL || !outR || size == 0)
        return;

    const LoopMode loop_mode = GetLoopMode();
    SnapshotMacroState_();
    SnapshotPLockState_();

    SampleEdit edit{};
    const Sample* edit_sample = nullptr;
    SnapshotRenderEditState_(edit, edit_sample);
    float engine_tune_scale[kEngineLayerCount] = {};
    float engine_voice_gain[kEngineLayerCount] = {};
    float lfo_depth = 0.0f;
    float env_amount = 0.0f;
    PrepareRenderScalars_(engine_tune_scale, engine_voice_gain, lfo_depth, env_amount);

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
    routes = SnapshotModRoutes_(routes_local);

    float route0_amount = (routes) ? routes_local[0].amount : 0.0f;
    Macros_Smooth(macro_smoothed_, active_macros_, macro_smooth_coeff_);
    Macros_Apply(macro_smoothed_, nullptr, &lfo_depth, &env_amount, &route0_amount, nullptr);

    if(routes)
    {
        routes_local[0].amount = route0_amount;
        routes = routes_local;
    }

    depth = lfo_depth;
    lfo_.SetRateHz(rate_hz);
    lfo_.SetWave(lfo_wave_);
    const float lfo_val = lfo_.Value();
    lfo_.TickBlock(size);
    const float lfo_src = lfo_val * depth;
    RefreshBlockState_(size);

    std::memset(outL, 0, sizeof(float) * size);
    std::memset(outR, 0, sizeof(float) * size);

    RecomputeLayerEmphasisCoeffs_(0u);
    RecomputeLayerEmphasisCoeffs_(1u);

    uint32_t active = 0;
    uint32_t clip_block = 0;
    float max_env = 0.0f;
    const float mix_scale = 0.7f;
    uint32_t playhead_frame[2] = {0u, 0u};
    uint32_t playhead_active[2] = {0u, 0u};
    float playhead_metric[2] = {-1.0f, -1.0f};
    const RenderVoiceContext render_ctx{
        outL,
        outR,
        size,
        loop_mode,
        edit,
        edit_sample,
        engine_tune_scale,
        engine_voice_gain,
        playhead_frame,
        playhead_active,
        playhead_metric,
    };

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
                if(r.dst == static_cast<uint8_t>(ModDest::Pitch))
                    mod_pitch += mod_val;
            }
        }

        if(mod_pitch > 1.0f)
            mod_pitch = 1.0f;
        if(mod_pitch < -1.0f)
            mod_pitch = -1.0f;
        const float pitch_scale = std::pow(2.0f, (mod_pitch * kPitchModSemitones) / 12.0f);

        if(v.state == VoiceState::StealXFade)
            RenderStealXFadeVoice_(v,
                                   render_ctx,
                                   pitch_scale,
                                   stop_fade_active,
                                   stop_fade_remaining,
                                   stop_fade_level,
                                   stop_fade_step);
        else
            RenderNormalVoice_(v,
                               render_ctx,
                               pitch_scale,
                               stop_fade_active,
                               stop_fade_remaining,
                               stop_fade_level,
                               stop_fade_step);
    }

    for(size_t i = 0; i < size; i++)
    {
        const float layer_a = ProcessLayerBusSample_(0u, outL[i]);
        const float layer_b = ProcessLayerBusSample_(1u, outR[i]);
        float mix = (layer_a + layer_b) * mix_scale;
        outL[i] = mix;
        outR[i] = mix;
        if(mix > 1.0f || mix < -1.0f)
            clip_block++;
    }

    WriteRenderDebug_(clip_block,
                      rate_hz,
                      depth,
                      lfo_src,
                      max_env,
                      active,
                      playhead_frame,
                      playhead_active);
}
