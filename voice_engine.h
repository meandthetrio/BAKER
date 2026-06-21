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
#include "express_state.h"

struct AppDiagnosticsState;
struct AppSharedState;

enum class VoiceState : uint8_t
{
    Idle = 0,
    Playing,
    Releasing,
    StealFadeOut,
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
    uint8_t    midi_note     = 0; // original MIDI note for note-off matching; never overwritten by steal/glide
    uint8_t    vel_layer     = 0;
    uint8_t    source_layer  = 0;
    uint32_t   start_id      = 0; // monotonic allocation id (used for Oldest Note stealing)

    const Sample* sample = nullptr;
    uint32_t      pos_frame = 0; // integer playback frame
    float         pos_frac  = 0.0f; // fractional offset within frame [0,1)
    float         ratio  = 1.0f; // playback increment per output sample
    float         gain   = 0.0f; // 0..1
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
    // Release time (ms) resolved at note-on from the perform ADSR + velmod,
    // mode-aware. NoteOff_ reuses this instead of recomputing from a base, so
    // one-shot voices honor the UI release and velmod release survives to
    // note-off.
    float         resolved_release_ms = 30.0f;
    // Velmod Phase 2b: per-voice send levels into the global effects, frozen at
    // note-on from velocity. Index 0=reverb, 1=delay, 2=sat. 0 = no send.
    // send_active is the fast-skip so voices with no send pay nothing in the
    // per-sample mix loop.
    float         send_level[3] = {0.0f, 0.0f, 0.0f};
    bool          send_active = false;
    bool          gate = false;
    bool          loop_voice = false;
    int8_t        dir  = 1;
    bool          poly_porto_glide_active = false;
    bool          poly_porto_managed = false;
    bool          poly_porto_source_valid = false;
    bool          poly_porto_source_released = false;
    uint8_t       poly_porto_source_note = 0;
    uint8_t       poly_porto_source_layer = 0;
    uint16_t      poly_porto_slide_samples_remaining = 0;
    float         poly_porto_current_semitones = 0.0f;
    float         poly_porto_target_semitones = 0.0f;
    float         poly_porto_step_semitones = 0.0f;
    uint32_t      poly_porto_source_order = 0;
    uint64_t      poly_porto_release_sample_time = 0;

    uint32_t old_pos_frame = 0;
    float old_pos_frac = 0.0f;
    float old_ratio = 1.0f;
    float old_gain  = 0.0f;
    uint8_t old_source_layer = 0;
    bool  old_gate  = false;
    int8_t old_dir  = 1;
    uint32_t new_pos_frame = 0;
    float new_pos_frac = 0.0f;
    float new_ratio = 1.0f;
    float new_gain  = 0.0f;
    uint8_t new_source_layer = 0;
    EnvStage new_env_stage = EnvStage::Off;
    float    new_env_level = 0.0f;
    float    new_env_a_step = 0.0f;
    float    new_env_d_step = 0.0f;
    float    new_env_r_step = 0.0f;
    float    new_env_sustain = 0.70f;
    bool     new_gate = false;
    bool     new_loop_voice = false;
    int8_t   new_dir  = 1;
    // Victim-only linear fade during steal (no dual-stream crossfade).
    float steal_fade_level = 1.0f;
    float steal_fade_step  = 0.0f;
};


class VoiceEngine
{
  public:
    static constexpr size_t kMaxVoices = 8;
    static constexpr uint8_t kMaxSampleBank = 8;

    void Init(float sample_rate, size_t block_size);
    void BindDiagnostics(AppDiagnosticsState* diagnostics) { diagnostics_ = diagnostics; }
    // Optional bind for cross-cutting shared state. Currently used by the
    // NoteOn handler to look up layer B's .bk slot (if a .bk is loaded, the
    // handler routes layer-1 note-ons to the per-slice Sample instead of
    // the regular sample bank). Safe to leave unbound — handler null-checks.
    void BindSharedState(AppSharedState* shared) { shared_ = shared; }

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
        const int slot = FindSampleBankSlot_(sample);
        if(slot < 0)
            return;
        sample_edit_bank_[slot] = edit;
        sample_edit_valid_[slot] = true;
        BumpSetupCacheGen_(); // affects cached region start/end/ls/le and edit_gain
    }
    void SetLpfCutoff(float hz) { lpf_cutoff_hz_ = hz; }
    void SetNormalizeEnabled(bool enabled) { normalize_enabled_ = enabled; }
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

    // AUDIO THREAD ONLY. Returns the smoothed macro state maintained
    // internally during RenderBlock; call this after RenderBlock to read
    // the up-to-date smoothed values. Avoids duplicating snapshot+smooth
    // state in the caller.
    const MacroState& SmoothedMacros() const { return macro_smoothed_; }
    void SetModParams(float lfo_rate_hz,
                      float lfo_depth,
                      float env_attack_ms,
                      float env_decay_ms,
                      float env_amount);
    void SetLfoWave(uint8_t wave);
    void SetEngineTuneSemitones(uint8_t layer, float semitones);
    void SetEngineLayerScale(uint8_t layer, float scale);
    void SetEngineLoopEnabled(uint8_t layer, bool enabled);
    void SetLoopEnvelopeParams(uint8_t layer,
                               float attack_ms,
                               float decay_ms,
                               float sustain_level,
                               float release_ms);
    // Per-layer attack/release curve: false = exponential (default), true =
    // logarithmic. Read at note-on (InitEnvelope) and note-off (SetEnvelopeRelease).
    void SetLoopEnvelopeCurves(uint8_t layer, bool attack_log, bool release_log);
    void SetLoopCrossfadeAmount(uint8_t layer, float amount);
    void SetLoopCrossfadeShape(uint8_t layer, float shape);
    // When true, the layer's sample already has the loop crossfade baked into its
    // PCM; the runtime seam crossfade and loop-boundary fade are both suppressed so
    // the loop is not smoothed twice.
    void SetLayerSeamBaked(uint8_t layer, bool baked);
    void SetPolyPortoEnabled(uint8_t layer, bool enabled);
    void SetPolyPortoVoiceLimit(uint8_t layer, uint8_t voice_limit);
    void SetPolyPortoSlideMs(uint8_t layer, float slide_ms);
    void SetPolyPortoSourceRangeSemitones(uint8_t layer, uint8_t semitones);
    void SetPolyPortoSourceMode(uint8_t layer, uint8_t source_mode);
    void SetPolyPortoReleaseMs(uint8_t layer, float release_ms);
    // Velocity-mod lane config (lane 0/1). target indexes the shared velmod
    // target list (0=----, 1=volume, 2=attack, 3=sustain, 4=release, 5..7
    // sends — sends are ignored here; applied in Phase 2b). amount -10..+10,
    // threshold 0..127, shape 0=knee/1=gate. Pushed once per block from the
    // audio callback.
    void SetVelMod(uint8_t lane,
                   uint8_t target,
                   int8_t  amount,
                   uint8_t threshold,
                   uint8_t shape,
                   uint8_t source);
    // When true (keyzone SPLIT), velmod lanes are routed by note relative to the
    // split note: notes <= split → lane 0 (Mod Block A), notes > split → lane 1
    // (Mod Block B). When false (FULL), both lanes apply to every note. Pushed
    // once per block from the callback.
    void SetVelModSplit(bool split) { velmod_split_ = split; }
    void SetVelModSplitNote(uint8_t note) { velmod_split_note_ = note; }
    // Keytrack volume (global): per-note gain across C1..C8 computed at note-on.
    // tilt = -12..+12 (sign picks which side is cut, magnitude = steepness),
    // amount_db = 0..12 the extremes reach (+/-), mid_note = 0 dB pivot. Inactive
    // at tilt 0 or amount 0.
    void SetKeytrack(int8_t tilt, int8_t amount_db, uint8_t mid_note)
    {
        keytrack_tilt_ = tilt;
        keytrack_amount_db_ = amount_db;
        keytrack_mid_note_ = mid_note;
    }
    void SetLoopMode(LoopMode mode)
    {
        const uint8_t old_v
            = loop_mode_.load(std::memory_order_relaxed);
        const uint8_t new_v = static_cast<uint8_t>(mode);
        if(old_v != new_v)
        {
            loop_mode_.store(new_v, std::memory_order_relaxed);
            BumpSetupCacheGen_(); // affects cached voice_loop_mode for non-loop voices
        }
    }
    LoopMode GetLoopMode() const
    {
        return static_cast<LoopMode>(loop_mode_.load(std::memory_order_relaxed));
    }

    // AUDIO THREAD ONLY: last block counters (use AppState atomics for UI).
    uint32_t ActiveLastBlock() const { return active_last_block_; }
    uint32_t StealsTotal() const { return steals_total_; }

    // Velmod 2b: per-effect send accumulators filled during RenderBlock.
    // Index 0=reverb, 1=delay, 2=sat. Valid until the next RenderBlock.
    // SendsActiveLastBlock() is false when no sounding voice had a send, so
    // the FX-chain injection can skip entirely.
    static constexpr uint8_t kSendBusCount = 3;
    const float* SendBus(uint8_t k) const { return send_bus_[k & 3u]; }
    bool SendsActiveLastBlock() const { return sends_any_active_; }
    // Per-effect: true if any sounding voice sent to effect k this block. Used
    // by the FX chain to run + inject only the effects that actually received
    // a send (0=reverb, 1=delay, 2=sat).
    bool SendBusActive(uint8_t k) const { return send_bus_active_[k & 3u]; }

  private:
    static constexpr size_t kSendBusMaxFrames = 48; // == max audio block size
    float   send_bus_[kSendBusCount][kSendBusMaxFrames] = {};
    bool    sends_any_active_ = false;
    bool    send_bus_active_[kSendBusCount] = {false, false, false};

    Voice*  voices_     = nullptr;
    float   sample_rate_ = 48000.0f;
    size_t  block_size_  = 48;
    float   block_release_coeff_ = 1.0f;
    uint32_t note_start_counter_ = 0;
    uint32_t active_last_block_ = 0;

    // Note-on storm flattening. A keyboard/chord slam can drop many NoteOns into
    // a single audio block; handling them all at once spikes the callback with
    // allocation + voice-steal + StartVoice_ setup stacked in one block (this is
    // the note-on-transient peak, not steady-state). Cap how many NoteOns are
    // fully processed per block and defer the rest to following blocks. At a
    // 48-sample block (~1 ms) the stagger is inaudible. Overflow of the stash
    // falls back to handling immediately — a CPU spike is preferable to a
    // dropped note.
    static constexpr uint8_t kMaxNoteOnsPerBlock = 2;
    static constexpr uint8_t kMaxDeferredNoteOns = 32;
    Event    deferred_note_ons_[kMaxDeferredNoteOns]{};
    uint8_t  deferred_note_on_count_ = 0;
    uint32_t steals_total_      = 0;
    uint32_t last_stolen_voice_index_ = 0;
    uint32_t last_stolen_start_id_    = 0;
    uint32_t last_new_start_id_       = 0;
    const Sample* sample_bank_[kMaxSampleBank] = {};
    uint8_t sample_bank_count_ = 0;
    const Sample* current_sample_     = nullptr;
    SampleEdit sample_edit_bank_[kMaxSampleBank]{};
    bool sample_edit_valid_[kMaxSampleBank] = {};
    float lpf_cutoff_hz_              = 20000.0f;
    bool  normalize_enabled_         = true;
    std::atomic<uint8_t> loop_mode_{static_cast<uint8_t>(LoopMode::Forward)};
    int32_t stop_fade_samples_        = 0;
    float lfo_rate_hz_ = 1.0f;
    float lfo_depth_   = 0.5f;
    uint8_t lfo_wave_  = 0;
    float env_attack_ms_ = 5.0f;
    float env_decay_ms_  = 120.0f;
    float env_amount_    = 0.5f;
    static constexpr uint8_t kEngineLayerCount = 1;
    static constexpr uint8_t kMaxVoicesPerLayer = 8;
    float engine_tune_semitones_[kEngineLayerCount] = {0.0f};
    // Cache of pow(2, semitones/12) per layer; populated lazily from inside
    // PrepareRenderScalars_ (const) when the dirty flag is set.
    mutable float engine_tune_scale_[kEngineLayerCount] = {1.0f};
    mutable bool  engine_tune_dirty_[kEngineLayerCount] = {true};
    float engine_layer_scale_[kEngineLayerCount]    = {1.0f};
    float pitch_mod_lut_[256] = {};
    bool  engine_loop_enabled_[kEngineLayerCount]   = {false};
    float loop_env_attack_ms_[kEngineLayerCount] = {5.0f};
    float loop_env_decay_ms_[kEngineLayerCount] = {20.0f};
    float loop_env_sustain_level_[kEngineLayerCount] = {1.0f};
    float loop_env_release_ms_[kEngineLayerCount] = {50.0f};
    bool  loop_env_attack_log_[kEngineLayerCount] = {false};
    bool  loop_env_release_log_[kEngineLayerCount] = {false};

    // Velocity-mod lane config (mirrors PerformParamsCurrent.velmod_*). Read at
    // note-on by StartVoice_. velmod_any_active_ is the fast-skip: true only if
    // a lane has a non-"----" target with non-zero amount, so the default state
    // costs a single branch in the note-on path.
    static constexpr uint8_t kVelModLaneCount = 2;
    uint8_t velmod_target_[kVelModLaneCount]    = {0u, 0u};
    int8_t  velmod_amount_[kVelModLaneCount]    = {0, 0};
    uint8_t velmod_threshold_[kVelModLaneCount] = {0u, 0u};
    uint8_t velmod_shape_[kVelModLaneCount]     = {0u, 0u};
    uint8_t velmod_source_[kVelModLaneCount]    = {0u, 0u}; // 0=>vel 1=<vel 2=>note 3=<note
    bool    velmod_any_active_ = false;
    bool    velmod_split_      = false;
    uint8_t velmod_split_note_ = 60u; // keyzone SPLIT divider (notes <= → A, > → B)
    int8_t  keytrack_tilt_      = 0;  // -12..+12 keytrack volume angle (0 = off)
    int8_t  keytrack_amount_db_ = 0;  // 0..12 dB +/- swing at the extremes
    uint8_t keytrack_mid_note_  = 66; // F#4 pivot (0 dB) the tent bends around
    // Returns the additive modulation fraction for `target_code` given this
    // note's velocity and note number, or 0 if no lane targets it / it's gated.
    // Each lane's source selects which value (velocity or note) drives the gate
    // + knee and the polarity (>= or <= threshold). Fraction is -1..+1 for
    // modifying targets (amount/10 * source-scale).
    float VelModFractionForTarget_(uint8_t target_code,
                                   uint8_t velocity,
                                   uint8_t note,
                                   uint8_t layer) const;
    float loop_crossfade_amount_[kEngineLayerCount] = {0.0625f};
    float loop_crossfade_shape_[kEngineLayerCount] = {0.0f};
    bool  layer_seam_baked_[kEngineLayerCount] = {false};

    // Per-voice cache of the block-constant portion of RenderNormalVoice_'s
    // setup. For a held voice in steady state none of the cached fields change
    // between blocks, so we skip the region-resolve / seam-frame / fade-threshold
    // recompute and reuse the cached values. Invalidated by:
    //   - sample pointer change for the voice (note-on, voice steal, sample swap)
    //   - source_layer or loop_voice change for the voice
    //   - global setup_cache_gen_ bump from any setter below
    //   - SetLoopCrossfadeAmount, SetLoopMode, SetSampleEdit, SetLayerSeamBaked,
    //     SetSampleBank, ResetSamples — bump via BumpSetupCacheGen_().
    struct VoiceBlockSetupCache
    {
        const Sample* sample        = nullptr;
        uint32_t      gen_seen      = 0u;
        bool          valid         = false;
        bool          loop_voice    = false;
        uint8_t       source_layer  = 0xFFu;
        uint32_t      start         = 0u;
        uint32_t      end           = 0u;
        uint32_t      ls_i          = 0u;
        uint32_t      le_i          = 0u;
        bool          loop_enabled  = false;
        bool          use_edit      = false;
        float         edit_gain     = 1.0f;
        int           slot          = -1;
        uint32_t      seam_frames   = 0u;
        uint32_t      crossfade_seam_frames = 0u;
        LoopMode      voice_loop_mode = LoopMode::Forward;
        float         length_f      = 0.0f;
        float         ls            = 0.0f;
        float         le            = 0.0f;
        float         loop_fade_frames            = 0.0f;
        float         loop_fade_start_threshold   = 0.0f;
        float         loop_fade_end_threshold     = 0.0f;
        bool          preview_sample = false;
    };
    VoiceBlockSetupCache voice_setup_cache_[kMaxVoices]{};
    // Starts at 1 so a default-constructed cache (gen_seen == 0) always misses.
    uint32_t setup_cache_gen_ = 1u;
    void BumpSetupCacheGen_() { ++setup_cache_gen_; }
    bool  poly_porto_enabled_[kEngineLayerCount] = {false};
    uint8_t poly_porto_voice_limit_[kEngineLayerCount]
        = {kExpressPolyPortoVoicesDefault};
    float poly_porto_slide_ms_[kEngineLayerCount]
        = {static_cast<float>(kExpressPolyPortoSlideDefaultMs)};
    uint8_t poly_porto_source_range_semitones_[kEngineLayerCount]
        = {kExpressPolyPortoRangeDefaultSemitones};
    uint8_t poly_porto_source_mode_[kEngineLayerCount]
        = {kExpressPolyPortoSourceClosest};
    float poly_porto_release_ms_[kEngineLayerCount]
        = {static_cast<float>(kExpressPolyPortoReleaseDefaultMs)};
    uint32_t poly_porto_source_order_counter_ = 0;
    uint64_t audio_sample_counter_ = 0;
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

    // See BindSharedState. May be nullptr; the NoteOn handler null-checks
    // before reading shared_->bk_layer_b.
    AppSharedState*        shared_              = nullptr;

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
    AppDiagnosticsState* diagnostics_ = nullptr;

    int  AllocateVoice_(uint8_t source_layer,
                        bool& stole,
                        uint8_t& stolen_index,
                        uint32_t& stolen_start_id);
    int  AllocateVoiceExcluding_(uint8_t source_layer,
                                 int exclude_index,
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
    // Full NoteOn handling (allocate/steal/start), factored out of ProcessEvents
    // so the per-block note-on cap can dispatch it from both the replay and the
    // live-drain paths.
    void HandleNoteOnEvent_(const Event& e);
    // Drop any still-deferred (un-sounded) note-ons for `note` so a note-off
    // can't leave a deferred press to sound later with no matching release.
    void CancelDeferredNoteOn_(uint8_t note);
    void ClearPolyPortoVoice_(Voice& v);
    void MarkPolyPortoHeldSource_(Voice& v);
    void MarkPolyPortoReleasedSource_(Voice& v);
    void BeginPolyPortoGlide_(Voice& v, uint8_t from_note, uint8_t to_note, float slide_ms);
    void BeginPolyPortoGlideFromAbsoluteNote_(Voice& v,
                                              float from_note,
                                              uint8_t to_note,
                                              float slide_ms);
    bool VoiceEligibleAsPolyPortoSource_(const Voice& v,
                                         uint8_t source_layer,
                                         uint8_t note,
                                         uint8_t range_semitones,
                                         float release_ms) const;
    float CurrentPolyPortoSourceAbsoluteNote_(const Voice& v) const;
    int  FindPolyPortoSourceVoice_(uint8_t source_layer,
                                   uint8_t note,
                                   uint8_t range_semitones,
                                   uint8_t source_mode,
                                   float release_ms) const;
    uint8_t CountActivePolyPortoVoices_(uint8_t source_layer, int exclude_index) const;
    bool TryStartPolyPortoVoice_(const Sample* sample,
                                 uint8_t note,
                                 uint8_t velocity,
                                 uint8_t source_layer,
                                 uint8_t vel_layer,
                                 uint32_t start_id,
                                 uint8_t& out_index);

    struct RenderVoiceContext
    {
        float* outL;
        float* outR;
        size_t size;
        LoopMode loop_mode;
        const float* engine_tune_scale;
        const float* engine_voice_gain;
        uint32_t* playhead_frame;
        uint32_t* playhead_active;
        float* playhead_metric;
        uint32_t* fetch_cycles;
        uint32_t* envmix_cycles;
        uint32_t* setup_cycles;
        uint32_t* presim_cycles;
        uint32_t* fetch_seam_cycles;
        uint32_t* fetch_seam_count;
        // Velmod 2b: global per-effect send accumulators (mono, block-sized).
        // Index 0=reverb, 1=delay, 2=sat. nullptr when no voice has sends.
        float* send_bus[3];
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
        uint32_t seam_frames;          // wrap offset
        uint32_t crossfade_seam_frames; // 0 when sample is seam-baked
        LoopMode voice_loop_mode;
        uint8_t  source_layer;
        float    gain;
        bool     use_edit;
        float    ratio;
        float    pitch_ratio_scale;
        float    fade_step;
        float    env_a_step;
        float    env_d_step;
        float    env_sustain;
        // P6: precomputed loop-boundary fade thresholds. When pos sits in
        // [loop_fade_start_threshold, loop_fade_end_threshold] we skip the
        // per-sample ComputeLoopBoundaryFade math entirely.
        float    loop_fade_frames;
        float    loop_fade_start_threshold;
        float    loop_fade_end_threshold;
        // P5: when true, the per-sample loop linearly ramps env_level by
        // env_per_sample_delta instead of running StepEnvelope every sample.
        // Decided at block start by RenderNormalVoice_ based on whether all
        // envelope stages are slow enough (>= 5 ms ≈ step ≤ 1/240).
        bool     block_rate_env;
        float    env_per_sample_delta;
        // Velmod 2b: copied from the voice at block start so the mix loops can
        // tap the per-sample voice output into the send buses.
        bool     send_active;
        float    send_level[3];
    };

    struct RenderStealFadeOutSetup
    {
        uint32_t start;
        uint32_t end;
        uint32_t ls_i;
        uint32_t le_i;
        bool     use_edit;
        float    edit_gain;
        uint8_t  old_layer;
        bool     old_loop_enabled;
        bool     loop_voice;
        float    length_f;
        float    ls;
        float    le;
        float    old_ratio;
        float    old_gain;
        uint32_t old_seam_frames;           // wrap offset
        uint32_t old_crossfade_seam_frames; // 0 when sample is seam-baked
        LoopMode old_loop_mode;
        float    loop_fade_frames;
        float    loop_fade_start_threshold;
        float    loop_fade_end_threshold;
        float    steal_fade_step;
    };

    struct StopFadeState
    {
        bool     active;
        int32_t  remaining;
        float    level;
        float    step;
    };

    struct RenderStealFadeOutLoopState
    {
        uint32_t old_pos_frame;
        float    old_pos_frac;
        bool     old_gate;
        int8_t   old_dir;
        float    steal_fade_level;
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
        // Sample-bank slot of v.sample, resolved once here so callers reuse it
        // (e.g. the record-preview check) instead of re-scanning the bank.
        int      slot;
    };

    void ResolveEffectivePlaybackRegion_(const Voice& v,
                                         const RenderVoiceContext& ctx,
                                         EffectivePlaybackRegion& out,
                                         bool& loop_enabled_base);

    bool StopFade_AdvanceAndFinishIfDone_(Voice& v, StopFadeState& sf);
    void BeginStopFadeOnStreamEnd_(Voice& v, StopFadeState& sf);
    void CompleteStealFadeOut_(Voice& v);
    // Per-sample steal-fade decrement (1/n), with the fade length scaled to the
    // stolen voice's amplitude: a quiet (e.g. attack-stage) victim needs only a
    // short anti-click fade, which also cuts steal-render cost. victim_gain is
    // the captured old_gain (gain*fade_in*env).
    float ComputeStealFadeStep_(float victim_gain) const;

    struct RenderNormalVoiceLoopState
    {
        uint32_t pos_frame;
        float    pos_frac;
        bool     gate;
        int8_t   dir;
        float    fade;
        EnvStage env_stage;
        float    env_level;
        float    env_r_step;
        float    ratio;
        bool     glide_active;
        float    glide_current_semitones;
        float    glide_target_semitones;
        float    glide_step_semitones;
        uint16_t glide_samples_remaining;
        StopFadeState stop_fade;
    };

    void CommitNormalVoiceLoopState_(Voice& v, const RenderNormalVoiceLoopState& st);
    void VoiceRender_PlayheadMetricIfAudible_(const Voice& v,
                                              const RenderVoiceContext& ctx,
                                              uint8_t ui_layer,
                                              uint32_t pos_frame,
                                              float pos_frac,
                                              float env_level);

    bool RenderNormalVoice_ProcessOneSample_(Voice& v,
                                             const RenderVoiceContext& ctx,
                                             const RenderNormalVoicePerBlockSetup& setup,
                                             size_t i,
                                             RenderNormalVoiceLoopState& st);
    // P2: batched render path for NormalVoice when block_rate_env is active.
    // Separates fetch + boundary-fade (Phase 1) from ramp + mix (Phase 2) so
    // the mix loop reduces to a tight multiply-add. Preserves the per-sample
    // ordering of stop-fade activation on end-of-stream. Caller is responsible
    // for pre-block setup (including block-rate env pre-simulation) and
    // post-loop state finalize/commit.
    void RenderNormalVoice_Batched_(Voice& v,
                                    const RenderVoiceContext& ctx,
                                    const RenderNormalVoicePerBlockSetup& setup,
                                    RenderNormalVoiceLoopState& st);
    // Fast-envelope (non-gliding) voices: same optimized batch fetch as the
    // slow path, but the envelope runs the per-sample StepEnvelope state machine
    // (inlined) instead of a linear ramp. Avoids the per-sample cross-TU fetch/
    // advance calls of RenderNormalVoice_ProcessOneSample_ that overran the CPU.
    void RenderNormalVoice_BatchedFastEnv_(Voice& v,
                                           const RenderVoiceContext& ctx,
                                           const RenderNormalVoicePerBlockSetup& setup,
                                           RenderNormalVoiceLoopState& st);
    bool RenderStealFadeOut_ProcessOneSample_(Voice& v,
                                              const RenderVoiceContext& ctx,
                                              const RenderStealFadeOutSetup& setup,
                                              size_t i,
                                              RenderStealFadeOutLoopState& st);

    void SnapshotMacroState_();
    void SnapshotPLockState_();
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
    void RenderStealFadeOutVoice_(Voice& v,
                                  const RenderVoiceContext& ctx,
                                  float pitch_scale,
                                  StopFadeState& stop_fade);
    void RenderNormalVoice_(Voice& v,
                            const RenderVoiceContext& ctx,
                            float pitch_scale,
                            StopFadeState& stop_fade);
    void RenderBlockMixLayers_(float* outL,
                               float* outR,
                               size_t size,
                               float mix_scale,
                               uint32_t& clip_block,
                               const bool (&layer_skip)[2],
                               uint32_t& sum_cycles);
    int  FindSampleBankSlot_(const Sample* sample) const;
    bool LookupSampleEdit_(const Sample* sample, SampleEdit& edit) const;

    static uint32_t PackVoiceDebug_(uint8_t idx, uint8_t note, uint8_t vel);
};
