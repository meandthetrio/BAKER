#pragma once

// Shared includes for voice_engine_render*.cpp leaf translation units.
#include "voice_engine.h"
#include "voice_engine_internal.h"

// Block-start / stepping helpers (voice_engine_render_playback.cpp). Used by
// voice_engine_render_voice.cpp only; keep render TU thin on playback math.
void VoicePlayback_NormalizeStealFadeOutPositions(uint32_t end_frame,
                                                  uint32_t ls,
                                                  uint32_t start,
                                                  bool loop_enabled,
                                                  uint32_t& pos_frame,
                                                  float& pos_frac,
                                                  bool& gate);

void VoicePlayback_NormalizeVoiceBlockStart(uint32_t end_frame,
                                            uint32_t ls,
                                            uint32_t start,
                                            bool loop_enabled,
                                            bool gate,
                                            uint32_t& pos_frame,
                                            float& pos_frac);

void VoicePlayback_ClampPosPastEndWhenGateOff(uint32_t end_frame,
                                              bool gate,
                                              uint32_t& pos_frame,
                                              float& pos_frac);

void VoicePlayback_ClampPosToLastFrameIfValid(uint32_t end_frame,
                                              uint32_t& pos_frame,
                                              float& pos_frac);

uint32_t VoicePlayback_ClampPlayheadFrame(uint32_t frame, uint32_t sample_length);

float VoicePlayback_PosAsFloat(uint32_t frame, float frac);

void VoicePlayback_StepFadeIn(float& fade, float fade_step);

float VoicePlayback_ClampMixToOne(float x);

float VoicePlayback_FadeInMultiplier(float fade);

// Loop stepping / sustain-loop seam / boundary sampling (voice_engine_render_loop.cpp).
bool AdvancePos(uint32_t& pos_frame,
                float& pos_frac,
                int8_t& dir,
                float ratio,
                uint32_t len,
                uint32_t ls,
                uint32_t le,
                bool loop_enabled,
                bool gate,
                LoopMode mode,
                uint32_t seam_offset = 0u);

bool VoiceRenderLoop_FullSampleWrapGate(const Sample* s,
                                        bool loop_enabled,
                                        bool gate);

float VoiceRenderLoop_ApplyBoundaryFadeNoSeam(float s,
                                              bool loop_voice,
                                              uint32_t seam_frames,
                                              bool used_seam_xfade,
                                              uint32_t pos_frame,
                                              float pos_frac,
                                              uint32_t start,
                                              uint32_t end,
                                              float sample_rate);

// P6 fast-path overload. Uses precomputed fade thresholds (populated once per
// block per voice) to short-circuit the ComputeLoopBoundaryFade call for
// samples sitting inside the loop interior. Falls through to the full fade
// math only when pos is within [start, start+fade_frames] or [end-fade_frames, end].
float VoiceRenderLoop_ApplyBoundaryFadeNoSeam(float s,
                                              bool loop_voice,
                                              uint32_t seam_frames,
                                              bool used_seam_xfade,
                                              uint32_t pos_frame,
                                              float pos_frac,
                                              float fade_start_threshold,
                                              float fade_end_threshold,
                                              uint32_t start,
                                              uint32_t end,
                                              float sample_rate);

// Sample fetch / interpolation (voice_engine_render_fetch.cpp).
void  VoiceRenderFetch_InitSqrtLut();
float SampleAtLinear(const Sample* s, uint32_t pos_frame, float pos_frac, bool wrap_end);
float SampleAtLinearRegion(const Sample* s,
                           uint32_t pos_frame,
                           float pos_frac,
                           uint32_t start,
                           uint32_t end,
                           bool loop_enabled,
                           uint32_t loop_start,
                           uint32_t loop_end);

uint32_t ComputeLoopSeamCrossfadeFrames(uint32_t start, uint32_t end, float amount);
float ComputeLoopSeamCrossfadeWeight(float mix, float shape, bool fade_in);
float SampleAtLoopSeamCrossfade(const Sample* s,
                                uint32_t pos_frame,
                                float pos_frac,
                                uint32_t start,
                                uint32_t end,
                                uint32_t seam_frames,
                                float shape,
                                float sample_rate,
                                bool& used_xfade);

float VoiceRenderFetch_VoiceStream(const Sample* sample,
                                   uint32_t pos_frame,
                                   float pos_frac,
                                   float gain,
                                   bool layer_loop_voice,
                                   uint32_t start,
                                   uint32_t end,
                                   uint32_t seam_frames,
                                   float loop_shape,
                                   float sample_rate,
                                   bool& used_seam_xfade,
                                   bool use_edit,
                                   bool region_loop_enabled,
                                   uint32_t ls_i,
                                   uint32_t le_i,
                                   bool gate_for_wrap);

// Per-voice, per-block fetch parameters used by the batched NormalVoice
// pipeline. Mirrors the arguments currently threaded through the per-sample
// VoiceRenderFetch_VoiceStream + VoiceRenderLoop_ApplyBoundaryFadeNoSeam +
// AdvancePos calls; all fields are block-scoped (set once by
// RenderNormalVoice_ before the inner loop).
struct VoiceBatchFetchParams
{
    const Sample* sample;
    uint32_t      start;
    uint32_t      end;
    uint32_t      ls_i;
    uint32_t      le_i;
    float         length_f;
    float         ls;
    float         le;
    bool          use_edit;
    bool          region_loop_enabled;
    bool          loop_enabled;
    LoopMode      voice_loop_mode;
    bool          layer_loop_voice;
    // Wrap offset: when seam_frames > 0, forward-loop wrap goes to start+seam_frames
    // (skipping the head region that's baked into the seam). Set for any looped
    // voice with a non-zero seam.
    uint32_t      seam_frames;
    // Live crossfade width: how many frames at end take the two-tap blend. 0 means
    // the sample is baked (seam data already contains the pre-blend), so playback
    // does a plain single-tap read. seam_frames stays non-zero so the wrap still
    // skips the baked head region.
    uint32_t      crossfade_seam_frames;
    float         loop_shape;
    float         sample_rate;
    float         ratio;
    float         gain;
    float         fade_start_threshold;
    float         fade_end_threshold;
};

// Block-rate ramp state for RenderNormalVoice_'s batched pipeline. Holds
// running env / fade-in / stop-fade values so the inner mix loop reduces to
// a tight multiply-add over the batched fetch buffer.
struct BatchRampState
{
    float    env_level;
    float    env_per_sample_delta;

    float    fade;
    float    fade_step;

    bool     sf_active;
    float    sf_level;
    float    sf_step;
    int32_t  sf_remaining;
};

// Fetches `count` samples starting at `pos`, applying gain + boundary fade.
// Advances `pos` in-place per sample. On end-of-stream mid-batch, sets
// `gate=false`, clamps pos to length-1, and continues filling the buffer with
// reads from the clamped boundary frame (matching the per-sample post-eos
// behaviour in RenderNormalVoice_ProcessOneSample_). Seam-crossfade and
// non-seam paths are both handled via the existing per-sample
// VoiceRenderFetch_VoiceStream + ApplyBoundaryFadeNoSeam helpers; the win
// comes from decoupling fetch from the ramp/mix state machine so the ramp
// loop becomes vectorisable.
//
// Returns the index of the first sample where AdvancePos failed (end of
// stream), or `count` if no end-of-stream occurred.
size_t VoiceRenderFetch_VoiceStreamBatch(const VoiceBatchFetchParams& p,
                                         uint32_t& pos_frame,
                                         float& pos_frac,
                                         int8_t& dir,
                                         bool& gate,
                                         size_t count,
                                         float* out_buf,
                                         uint32_t* seam_cycles = nullptr,
                                         uint32_t* seam_count  = nullptr);
