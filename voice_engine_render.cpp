#include "voice_engine_internal.h"

#include "app_state_diagnostics.h"
#include "build_config.h"

#include <cmath>
#include <cstring>

static constexpr float kReleaseTauSec      = 0.030f;
static constexpr float kPitchModSemitones  = 1.0f;
static constexpr float kMacroSmoothSec     = 0.005f;
static constexpr float kEngineTuneMinSemitones = -48.0f;
static constexpr float kEngineTuneMaxSemitones = 48.0f;

// P1: threshold below which the emphasis ladder/DC-blocker state is considered
// inaudibly decayed (summed |state| across the four ladder poles + the DC
// blocker output). ~1e-5 ≈ -100 dB full-scale, well below the noise floor of
// any downstream stage. Paired with layer activity tracking to avoid clicks
// when a new voice engages after a skip.
static constexpr float kEmphasisStateEpsilon = 1e-5f;

static inline float LayerBusState_StateMagnitude(const LayerBusState& s)
{
    // drive_dc_x is "previous input"; once input is zero for one sample it's
    // also zero, so it does not need to participate in the magnitude check.
    return std::fabs(s.pole1) + std::fabs(s.pole2)
         + std::fabs(s.pole3) + std::fabs(s.pole4)
         + std::fabs(s.drive_dc_y);
}

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
        // Recompute pow(2, semitones/12) only when the per-layer tune target
        // actually changed (via SetEngineTuneSemitones). Steady-state reads
        // hit the cached scale and avoid the std::pow on the audio thread.
        if(engine_tune_dirty_[layer])
        {
            float tune = engine_tune_semitones_[layer];
            if(tune < kEngineTuneMinSemitones)
                tune = kEngineTuneMinSemitones;
            if(tune > kEngineTuneMaxSemitones)
                tune = kEngineTuneMaxSemitones;
            engine_tune_scale_[layer] = std::pow(2.0f, tune / 12.0f);
            engine_tune_dirty_[layer] = false;
        }
        engine_tune_scale[layer] = engine_tune_scale_[layer];
        engine_voice_gain[layer] = 1.0f;
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


void VoiceEngine::RenderBlock(float* outL, float* outR, size_t size)
{
    if(!outL || !outR || size == 0)
        return;

    const bool diag_on = diagnostics_ != nullptr;
    uint32_t render_total_start = 0u;
    uint32_t fixed_setup_cycles = 0u;
    uint32_t clear_buffers_cycles = 0u;
    uint32_t active_voices_cycles = 0u;
    uint32_t normal_voices_cycles = 0u;
    uint32_t steal_voices_cycles = 0u;
    uint32_t fetch_cycles = 0u;
    uint32_t envmix_cycles = 0u;
    uint32_t voice_setup_cycles = 0u;
    uint32_t env_presim_cycles = 0u;
    uint32_t fetch_seam_cycles = 0u;
    uint32_t fetch_seam_count = 0u;
    uint32_t layer_mix_cycles = 0u;
    if(diag_on)
        render_total_start = DWT->CYCCNT;

    const LoopMode loop_mode = GetLoopMode();
    uint32_t setup_start = 0u;
    if(diag_on)
        setup_start = DWT->CYCCNT;
#if MOD_SYSTEM_ENABLED
    SnapshotMacroState_();
#endif
    SnapshotPLockState_();

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
    float lfo_src = 0.0f;
#if MOD_SYSTEM_ENABLED
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
    lfo_src = lfo_val * depth;
#else
    // Modulation system disabled at build time: no route snapshot, no macro
    // smoothing/apply, no LFO sinf/tick. routes stays null and lfo_src 0.
    (void)rate_hz;
    (void)depth;
    (void)lfo_depth;
    (void)env_amount;
    (void)routes;
#endif
    RefreshBlockState_(size);
    if(diag_on)
        fixed_setup_cycles += DWT->CYCCNT - setup_start;

    uint32_t clear_start = 0u;
    if(diag_on)
        clear_start = DWT->CYCCNT;
    std::memset(outL, 0, sizeof(float) * size);
    std::memset(outR, 0, sizeof(float) * size);
    if(diag_on)
        clear_buffers_cycles = DWT->CYCCNT - clear_start;

    if(diag_on)
        setup_start = DWT->CYCCNT;
    for(uint8_t layer = 0u; layer < kEngineLayerCount; ++layer)
    {
        if(emphasis_dirty_[layer])
        {
            RecomputeLayerEmphasisCoeffs_(layer);
            emphasis_dirty_[layer] = false;
        }
    }
    if(diag_on)
        fixed_setup_cycles += DWT->CYCCNT - setup_start;

    uint32_t active = 0;
    uint32_t clip_block = 0;
    float max_env = 0.0f;
    const float mix_scale = 0.7f;
    uint32_t playhead_frame[2] = {0u, 0u};
    uint32_t playhead_active[2] = {0u, 0u};
    float playhead_metric[2] = {-1.0f, -1.0f};
    // P1: true for any layer a voice writes to this block (normal voice writes
    // its source_layer; StealFadeOut writes only the victim/old layer until
    // fade completes).
    bool layer_active[2] = {false, false};

    // Velmod 2b: detect which effects any sounding voice sends to this block.
    // Only then do we clear + route the send buses; otherwise the FX-chain
    // injection and the per-sample tap are skipped (default = zero overhead).
    bool sends_any = false;
    bool send_eff[kSendBusCount] = {false, false, false};
    for(size_t vi = 0; vi < kMaxVoices; ++vi)
    {
        const Voice& vv = voices_[vi];
        if(vv.state == VoiceState::Idle || !vv.send_active)
            continue;
        sends_any = true;
        for(uint8_t k = 0; k < kSendBusCount; ++k)
            if(vv.send_level[k] != 0.0f)
                send_eff[k] = true;
    }
    sends_any_active_ = sends_any;
    for(uint8_t k = 0; k < kSendBusCount; ++k)
        send_bus_active_[k] = send_eff[k];
    if(sends_any)
    {
        // Clear all routed buses (the per-sample tap writes to all three for
        // branch simplicity; unused effects just accumulate zeros).
        const size_t n = (size < kSendBusMaxFrames) ? size : kSendBusMaxFrames;
        for(uint8_t k = 0; k < kSendBusCount; ++k)
            std::memset(send_bus_[k], 0, n * sizeof(float));
    }

    const RenderVoiceContext render_ctx{
        outL,
        outR,
        size,
        loop_mode,
        engine_tune_scale,
        engine_voice_gain,
        playhead_frame,
        playhead_active,
        playhead_metric,
        diag_on ? &fetch_cycles : nullptr,
        diag_on ? &envmix_cycles : nullptr,
        diag_on ? &voice_setup_cycles : nullptr,
        diag_on ? &env_presim_cycles : nullptr,
        diag_on ? &fetch_seam_cycles : nullptr,
        diag_on ? &fetch_seam_count : nullptr,
        {sends_any ? send_bus_[0] : nullptr,
         sends_any ? send_bus_[1] : nullptr,
         sends_any ? send_bus_[2] : nullptr},
    };

    for(size_t vi = 0; vi < kMaxVoices; vi++)
    {
        Voice& v = voices_[vi];
        if(v.state == VoiceState::Idle)
            continue;

        const bool is_steal_voice = (v.state == VoiceState::StealFadeOut);
        const uint32_t active_start = diag_on ? DWT->CYCCNT : 0u;

        StopFadeState stop_fade{};
        stop_fade.active = v.stop_fade_active;
        stop_fade.remaining = v.stop_fade_samples_remaining;
        stop_fade.level = v.stop_fade_level;
        stop_fade.step = v.stop_fade_step;

        active++;

        float pitch_scale = 1.0f;
#if MOD_SYSTEM_ENABLED
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
        if(mod_pitch != 0.0f)
        {
            int lut_idx = static_cast<int>((mod_pitch + 1.0f) * 127.5f);
            if(lut_idx < 0)   lut_idx = 0;
            if(lut_idx > 255) lut_idx = 255;
            pitch_scale = pitch_mod_lut_[lut_idx];
        }
#endif

        if(is_steal_voice)
        {
            layer_active[v.old_source_layer & 1u] = true;
            RenderStealFadeOutVoice_(v, render_ctx, pitch_scale, stop_fade);
        }
        else
        {
            layer_active[v.source_layer & 1u] = true;
            RenderNormalVoice_(v, render_ctx, pitch_scale, stop_fade);
        }

        if(diag_on)
        {
            const uint32_t voice_cycles = DWT->CYCCNT - active_start;
            active_voices_cycles += voice_cycles;
            if(is_steal_voice)
                steal_voices_cycles += voice_cycles;
            else
                normal_voices_cycles += voice_cycles;
        }
    }

    // P1: skip per-sample emphasis for layers that (a) had no voice writing to
    // them this block AND (b) whose ladder/DC-blocker state has decayed below
    // an inaudible epsilon. Either condition alone would risk a click when a
    // new voice engages — the gate combines both so fresh input always
    // traverses (already-settled) state.
    const float state_mag_a = LayerBusState_StateMagnitude(layer_bus_state_[0]);
    const float state_mag_b = LayerBusState_StateMagnitude(layer_bus_state_[1]);
    const bool layer_skip[2] = {
        !layer_active[0] && state_mag_a < kEmphasisStateEpsilon,
        !layer_active[1] && state_mag_b < kEmphasisStateEpsilon,
    };
    uint32_t emphasis_cycles = 0u;
    uint32_t sum_cycles = 0u;
    RenderBlockMixLayers_(
        outL, outR, size, mix_scale, clip_block, layer_skip, emphasis_cycles, sum_cycles);
    // layer_mix bucket = whole layer stage (emphasis ladder + stereo sum), now with
    // the gain-probe meters excluded so the reading is truthful while the overlay is open.
    layer_mix_cycles = emphasis_cycles + sum_cycles;

    WriteRenderDebug_(clip_block,
                      rate_hz,
                      depth,
                      lfo_src,
                      max_env,
                      active,
                      playhead_frame,
                      playhead_active);

    if(diag_on)
    {
        const uint32_t render_total_cycles = DWT->CYCCNT - render_total_start;
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketRenderTotal, render_total_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketFixedSetup, fixed_setup_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketClearBuffers, clear_buffers_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketActiveVoicesTotal, active_voices_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketNormalVoices, normal_voices_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketStealVoices, steal_voices_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketFetch, fetch_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketEnvMix, envmix_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketLayerMix, layer_mix_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketLayerEmphasis, emphasis_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketVoiceSetup, voice_setup_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketEnvPresim, env_presim_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketFetchSeamCycles, fetch_seam_cycles);
        DiagnosticsStoreVoiceCycleBucket(
            *diagnostics_, kDiagVoiceBucketFetchSeamCount, fetch_seam_count);
    }

    audio_sample_counter_ += static_cast<uint64_t>(size);
}
