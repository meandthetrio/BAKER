#pragma once

// `voice_engine.*` implements a fixed-size voice pool that runs entirely in the audio thread.
// It is driven by `EventQueueSPSC` note events (NoteOn/NoteOff) and renders PCM sample playback.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "event_queue.h"
#include "sampler_sample.h"
#include "sample_edit.h"
#include "mod_sources.h"
#include "mod_matrix.h"
#include "plocks.h"
#include "macros.h"

enum class VoiceState : uint8_t
{
    Idle = 0,
    Playing,
    Releasing,
    StealXFade,
};

enum class EnvStage : uint8_t
{
    Off = 0,
    Attack,
    Decay,
    Sustain,
    Release,
};

enum class LoopMode : uint8_t
{
    Forward = 0,
    PingPong,
};

struct Voice
{
    VoiceState state         = VoiceState::Idle;
    EnvStage   env_stage     = EnvStage::Off;
    uint8_t    note          = 0;
    uint8_t    velocity      = 0;
    uint8_t    _pad0         = 0;
    uint8_t    vel_layer     = 0;
    uint8_t    source_layer  = 0;
    uint32_t   start_id      = 0; // monotonic allocation id (used for Oldest Note stealing)

    const Sample* sample = nullptr;
    float         pos    = 0.0f; // playback position in samples
    float         ratio  = 1.0f; // playback increment per output sample
    float         gain   = 0.0f; // 0..1
    float         vel_brightness = 1.0f;
    float         lpf_z  = 0.0f; // low state for filter
    float         lpf_bp = 0.0f; // band state for resonant filter
    ModEnv        mod_env;
    float         fade_in = 0.0f; // 0..1
    float         fade_in_step = 0.0f; // per-sample increment
    bool          stop_fade_active = false;
    int32_t       stop_fade_samples_remaining = 0;
    float         stop_fade_level = 0.0f; // 1..0
    float         stop_fade_step = 0.0f; // per-sample decrement
    float         release_coeff = 1.0f; // per-block exp decay (30ms)
    float         env_level  = 0.0f; // 0..1
    float         env_a_step = 0.0f;
    float         env_d_step = 0.0f;
    float         env_r_step = 0.0f;
    float         env_sustain = 0.70f;
    bool          gate = false;
    bool          loop_voice = false;
    int8_t        dir  = 1;

    float old_pos   = 0.0f;
    float old_ratio = 1.0f;
    float old_gain  = 0.0f;
    uint8_t old_source_layer = 0;
    bool  old_gate  = false;
    int8_t old_dir  = 1;
    float new_pos   = 0.0f;
    float new_ratio = 1.0f;
    float new_gain  = 0.0f;
    uint8_t new_source_layer = 0;
    float new_fade_in = 0.0f; // 0..1 for new head
    float new_fade_in_step = 0.0f; // per-sample increment for new head
    EnvStage new_env_stage = EnvStage::Off;
    float    new_env_level = 0.0f;
    float    new_env_a_step = 0.0f;
    float    new_env_d_step = 0.0f;
    float    new_env_r_step = 0.0f;
    float    new_env_sustain = 0.70f;
    bool     new_gate = false;
    bool     new_loop_voice = false;
    int8_t   new_dir  = 1;
    float xfade_pos  = 0.0f; // 0..1
    float xfade_step = 0.0f; // per-sample increment
};

struct LayerBusState
{
    float drive_dc_x = 0.0f;
    float drive_dc_y = 0.0f;
    float pole1 = 0.0f;
    float pole2 = 0.0f;
    float pole3 = 0.0f;
    float pole4 = 0.0f;
};

// Per-block cached scalars for EMPHASIS (drive + ladder); poles/DC stay in LayerBusState.
struct LayerEmphasisCoeffs
{
    bool  odd_drive = true;
    float pre_gain = 1.0f;
    float shape_blend = 0.0f;
    float base_makeup = 1.0f;
    float positive_drive = 1.0f;
    float negative_drive = 1.0f;
    float even_makeup = 1.0f;
    float g = 0.0f;
    float feedback = 0.0f;
    float pole4_linear_scale = 1.0f;
    float tanh_input_scale = 1.12f;
};

class VoiceEngine
{
  public:
    static constexpr size_t kMaxVoices = 10;
    static constexpr uint8_t kMaxSampleBank = 8;

    void Init(float sample_rate, size_t block_size);

    // AUDIO THREAD ONLY: pops and handles all pending events for this block.
    void ProcessEvents(EventQueueSPSC& q);

    // AUDIO THREAD ONLY: clears and fills `outL/outR` with summed voice audio.
    void RenderBlock(float* outL, float* outR, size_t size);

    // Optional debug bindings:
    // - `events_popped`: incremented once per event popped from the queue
    // - `voices_active`: set once per block to active voice count
    // - `voice_steals`: incremented when allocating with no free voice
    // - `last_voice_packed`: packed {idx, note, vel} in low 24 bits for OLED
    void BindDebug(std::atomic<uint32_t>* events_popped,
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
                   std::atomic<uint32_t>* playhead_active_b);

    void SetSampleBank(const Sample* const* bank, uint8_t count);
    void SetSample(const Sample* sample) { current_sample_ = sample; }
    void SetSampleEdit(const SampleEdit& edit, const Sample* sample)
    {
        current_edit_ = edit;
        edit_sample_ = sample;
    }
    void SetLpfCutoff(float hz) { lpf_cutoff_hz_ = hz; }
    void SetModMatrix(const ModMatrixState* state) { mod_matrix_ = state; }
    void SetPLocks(const PLocksState* state) { plocks_ = state; }
    void SetMacros(const MacroState* a,
                   const MacroState* b,
                   const std::atomic<uint8_t>* sel,
                   const std::atomic<uint32_t>* gen)
    {
        macro_a_ = a;
        macro_b_ = b;
        macro_sel_ = sel;
        macro_gen_ = gen;
    }
    void SetModParams(float lfo_rate_hz,
                      float lfo_depth,
                      float env_attack_ms,
                      float env_decay_ms,
                      float env_amount);
    void SetLfoWave(uint8_t wave);
    void SetEngineTuneSemitones(uint8_t layer, float semitones);
    void SetEngineGainDb(uint8_t layer, float db);
    void SetEngineDriveMode(uint8_t layer, uint8_t mode);
    void SetEngineLayerScale(uint8_t layer, float scale);
    void SetEngineFilterCutoffHz(uint8_t layer, float hz);
    void SetEngineFilterResonance(uint8_t layer, float resonance);
    void SetEngineLoopEnabled(uint8_t layer, bool enabled);
    void SetLoopEnvelopeParams(uint8_t layer,
                               float attack_ms,
                               float decay_ms,
                               float sustain_level,
                               float release_ms);
    void SetLoopCrossfadeAmount(uint8_t layer, float amount);
    void SetLoopCrossfadeShape(uint8_t layer, float shape);
    void SetLoopMode(LoopMode mode)
    {
        loop_mode_.store(static_cast<uint8_t>(mode), std::memory_order_relaxed);
    }
    LoopMode GetLoopMode() const
    {
        return static_cast<LoopMode>(loop_mode_.load(std::memory_order_relaxed));
    }

    // AUDIO THREAD ONLY: last block counters (use AppState atomics for UI).
    uint32_t ActiveLastBlock() const { return active_last_block_; }
    uint32_t StealsTotal() const { return steals_total_; }

  private:
    Voice*  voices_     = nullptr;
    float   sample_rate_ = 48000.0f;
    size_t  block_size_  = 48;
    float   block_release_coeff_ = 1.0f;
    uint32_t note_start_counter_ = 0;
    uint32_t active_last_block_ = 0;
    uint32_t steals_total_      = 0;
    uint32_t last_stolen_voice_index_ = 0;
    uint32_t last_stolen_start_id_    = 0;
    uint32_t last_new_start_id_       = 0;
    const Sample* sample_bank_[kMaxSampleBank] = {};
    uint8_t sample_bank_count_ = 0;
    const Sample* current_sample_     = nullptr;
    const Sample* edit_sample_        = nullptr;
    SampleEdit current_edit_{};
    float lpf_cutoff_hz_              = 20000.0f;
    std::atomic<uint8_t> loop_mode_{static_cast<uint8_t>(LoopMode::Forward)};
    int32_t stop_fade_samples_        = 0;
    float lfo_rate_hz_ = 1.0f;
    float lfo_depth_   = 0.5f;
    uint8_t lfo_wave_  = 0;
    float env_attack_ms_ = 5.0f;
    float env_decay_ms_  = 120.0f;
    float env_amount_    = 0.5f;
    static constexpr uint8_t kEngineLayerCount = 2;
    static constexpr uint8_t kMaxVoicesPerLayer = 5;
    float engine_tune_semitones_[kEngineLayerCount] = {0.0f, 0.0f};
    float engine_gain_linear_[kEngineLayerCount]    = {1.0f, 1.0f};
    uint8_t engine_drive_mode_[kEngineLayerCount]   = {0u, 0u};
    float engine_layer_scale_[kEngineLayerCount]    = {1.0f, 1.0f};
    float engine_filter_cutoff_hz_[kEngineLayerCount] = {20000.0f, 20000.0f};
    float engine_filter_resonance_[kEngineLayerCount] = {0.0f, 0.0f};
    LayerBusState layer_bus_state_[kEngineLayerCount]{};
    LayerEmphasisCoeffs emphasis_coeff_[kEngineLayerCount]{};
    bool  engine_loop_enabled_[kEngineLayerCount]   = {false, false};
    float loop_env_attack_ms_[kEngineLayerCount] = {5.0f, 5.0f};
    float loop_env_decay_ms_[kEngineLayerCount] = {20.0f, 20.0f};
    float loop_env_sustain_level_[kEngineLayerCount] = {1.0f, 1.0f};
    float loop_env_release_ms_[kEngineLayerCount] = {50.0f, 50.0f};
    float loop_crossfade_amount_[kEngineLayerCount] = {0.0625f, 0.0625f};
    float loop_crossfade_shape_[kEngineLayerCount] = {0.0f, 0.0f};
    GlobalLFO lfo_;
    float sweep_phase_rate_  = 0.0f;
    float sweep_dir_rate_    = 1.0f;
    float sweep_phase_depth_ = 0.0f;
    float sweep_dir_depth_   = 1.0f;
    const ModMatrixState* mod_matrix_ = nullptr;
    const PLocksState* plocks_        = nullptr;
    StepLock active_lock_{};
    uint32_t lock_gen_seen_ = 0;
    const MacroState* macro_a_ = nullptr;
    const MacroState* macro_b_ = nullptr;
    const std::atomic<uint8_t>* macro_sel_ = nullptr;
    const std::atomic<uint32_t>* macro_gen_ = nullptr;
    MacroState active_macros_{};
    MacroState macro_smoothed_{};
    uint32_t macro_gen_seen_ = 0;
    float macro_smooth_coeff_ = 1.0f;

    std::atomic<uint32_t>* events_popped_      = nullptr;
    std::atomic<uint32_t>* voices_active_      = nullptr;
    std::atomic<uint32_t>* voice_steals_       = nullptr;
    std::atomic<uint32_t>* last_voice_packed_  = nullptr;
    std::atomic<uint32_t>* last_stolen_voice_index_out_ = nullptr;
    std::atomic<uint32_t>* last_stolen_start_id_out_    = nullptr;
    std::atomic<uint32_t>* last_new_start_id_out_       = nullptr;
    std::atomic<uint32_t>* clip_count_         = nullptr;
    std::atomic<uint32_t>* fadeouts_started_   = nullptr;
    std::atomic<int32_t>* last_lfo_out_        = nullptr;
    std::atomic<int32_t>* last_env_out_        = nullptr;
    std::atomic<uint32_t>* lfo_rate_dbg_out_   = nullptr;
    std::atomic<uint32_t>* lfo_depth_dbg_out_  = nullptr;
    std::atomic<uint32_t>* playhead_frame_out_[2] = {nullptr, nullptr};
    std::atomic<uint32_t>* playhead_active_out_[2] = {nullptr, nullptr};

    int  AllocateVoice_(uint8_t source_layer,
                        bool& stole,
                        uint8_t& stolen_index,
                        uint32_t& stolen_start_id);
    void StartVoice_(Voice& v,
                     const Sample* sample,
                     uint8_t note,
                     uint8_t velocity,
                     uint8_t source_layer,
                     uint8_t vel_layer,
                     uint32_t start_id);
    void StartStopFade_(Voice& v);
    void FinishStopFade_(Voice& v);
    void NoteOff_(uint8_t note);
    void AllNotesOff_();
    void RecomputeLayerEmphasisCoeffs_(uint8_t layer);
    float ProcessLayerBusSample_(uint8_t layer, float input);

    struct RenderVoiceContext
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

    struct RenderNormalVoicePerBlockSetup
    {
        uint32_t start;
        uint32_t end;
        float    length_f;
        float    ls;
        float    le;
        uint32_t ls_i;
        uint32_t le_i;
        bool     loop_enabled;
        bool     loop_voice;
        uint32_t seam_frames;
        LoopMode voice_loop_mode;
        uint8_t  source_layer;
        float    gain;
        bool     use_edit;
        float    ratio;
        float    fade_step;
        float    env_a_step;
        float    env_d_step;
        float    env_sustain;
    };

    struct RenderStealXFadeSetup
    {
        uint32_t start;
        uint32_t end;
        uint32_t ls_i;
        uint32_t le_i;
        bool     use_edit;
        float    edit_gain;
        uint8_t  old_layer;
        uint8_t  new_layer;
        bool     old_loop_enabled;
        bool     new_loop_enabled;
        bool     loop_voice;
        bool     new_loop_voice;
        float    length_f;
        float    ls;
        float    le;
        float    old_ratio;
        float    new_ratio;
        float    old_gain;
        float    new_gain;
        uint32_t old_seam_frames;
        uint32_t new_seam_frames;
        LoopMode old_loop_mode;
        LoopMode new_loop_mode;
        float    x_step;
        float    new_fade_step;
        float    new_env_a_step;
        float    new_env_d_step;
        float    new_env_sustain;
    };

    struct StopFadeState
    {
        bool     active;
        int32_t  remaining;
        float    level;
        float    step;
    };

    struct RenderStealXFadeLoopState
    {
        float    old_pos;
        float    new_pos;
        bool     old_gate;
        bool     new_gate;
        int8_t   old_dir;
        int8_t   new_dir;
        float    xfade_pos;
        float    new_fade_in;
        EnvStage new_env_stage;
        float    new_env_level;
        float    new_env_r_step;
        StopFadeState stop_fade;
    };

    struct EffectivePlaybackRegion
    {
        uint32_t start;
        uint32_t end;
        uint32_t ls_i;
        uint32_t le_i;
        float    edit_gain;
        bool     use_edit;
    };

    void ResolveEffectivePlaybackRegion_(const Voice& v,
                                         const RenderVoiceContext& ctx,
                                         EffectivePlaybackRegion& out,
                                         bool& loop_enabled_base);

    bool StopFade_AdvanceAndFinishIfDone_(Voice& v, StopFadeState& sf);
    void BeginStopFadeOnStreamEnd_(Voice& v, StopFadeState& sf);
    void CompleteStealXFade_(Voice& v);

    struct RenderNormalVoiceLoopState
    {
        float    pos;
        bool     gate;
        int8_t   dir;
        float    fade;
        EnvStage env_stage;
        float    env_level;
        float    env_r_step;
        StopFadeState stop_fade;
    };

    void CommitNormalVoiceLoopState_(Voice& v, const RenderNormalVoiceLoopState& st);
    void VoiceRender_PlayheadMetricIfAudible_(const Voice& v,
                                              const RenderVoiceContext& ctx,
                                              uint8_t ui_layer,
                                              float pos,
                                              float env_level);

    bool RenderNormalVoice_ProcessOneSample_(Voice& v,
                                             const RenderVoiceContext& ctx,
                                             const RenderNormalVoicePerBlockSetup& setup,
                                             size_t i,
                                             RenderNormalVoiceLoopState& st);
    bool RenderStealXFade_ProcessOneSample_(Voice& v,
                                            const RenderVoiceContext& ctx,
                                            const RenderStealXFadeSetup& setup,
                                            size_t i,
                                            RenderStealXFadeLoopState& st);

    void SnapshotMacroState_();
    void SnapshotPLockState_();
    void SnapshotRenderEditState_(SampleEdit& edit, const Sample*& edit_sample) const;
    const ModRoute* SnapshotModRoutes_(ModRoute (&routes_local)[kMaxModRoutes]) const;
    void PrepareRenderScalars_(float (&engine_tune_scale)[kEngineLayerCount],
                               float (&engine_voice_gain)[kEngineLayerCount],
                               float& lfo_depth,
                               float& env_amount) const;
    void RefreshBlockState_(size_t size);
    void WriteRenderDebug_(uint32_t clip_block,
                           float rate_hz,
                           float depth,
                           float lfo_src,
                           float max_env,
                           uint32_t active,
                           const uint32_t (&playhead_frame)[2],
                           const uint32_t (&playhead_active)[2]);
    void RenderStealXFadeVoice_(Voice& v,
                                const RenderVoiceContext& ctx,
                                float pitch_scale,
                                StopFadeState& stop_fade);
    void RenderNormalVoice_(Voice& v,
                            const RenderVoiceContext& ctx,
                            float pitch_scale,
                            StopFadeState& stop_fade);
    void RenderBlockMixLayers_(float* outL, float* outR, size_t size, float mix_scale, uint32_t& clip_block);

    static uint32_t PackVoiceDebug_(uint8_t idx, uint8_t note, uint8_t vel);
};
