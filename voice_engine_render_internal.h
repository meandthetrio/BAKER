#pragma once

// Shared includes for voice_engine_render*.cpp leaf translation units.
#include "voice_engine.h"
#include "voice_engine_internal.h"

// Block-start / stepping helpers (voice_engine_render_playback.cpp). Used by
// voice_engine_render_voice.cpp only; keep render TU thin on playback math.
void VoicePlayback_NormalizeStealXFadePositions(float length_f,
                                                float ls,
                                                uint32_t start,
                                                bool old_loop_enabled,
                                                bool new_loop_enabled,
                                                float& old_pos,
                                                float& new_pos,
                                                bool& old_gate,
                                                bool& new_gate);

void VoicePlayback_NormalizeVoiceBlockStart(float length_f,
                                            float ls,
                                            uint32_t start,
                                            bool loop_enabled,
                                            bool gate,
                                            float& pos);

void VoicePlayback_ClampPosPastEndWhenGateOff(float length_f, bool gate, float& pos);

void VoicePlayback_ClampPosToLastFrameIfValid(float length_f, float& pos);

float VoicePlayback_ClampPlayheadPos(float p, uint32_t sample_length);

void VoicePlayback_StepFadeIn(float& fade, float fade_step);

float VoicePlayback_ClampMixToOne(float x);

float VoicePlayback_FadeInMultiplier(float fade);

// Loop stepping / sustain-loop seam / boundary sampling (voice_engine_render_loop.cpp).
bool AdvancePos(float& pos,
                int8_t& dir,
                float ratio,
                float len,
                float ls,
                float le,
                bool loop_enabled,
                bool gate,
                LoopMode mode,
                float seam_offset = 0.0f);

bool VoiceRenderLoop_FullSampleWrapGate(const Sample* s,
                                        bool loop_enabled,
                                        bool gate);

float VoiceRenderLoop_ApplyBoundaryFadeNoSeam(float s,
                                              bool loop_voice,
                                              uint32_t seam_frames,
                                              bool used_seam_xfade,
                                              float pos,
                                              uint32_t start,
                                              uint32_t end,
                                              float sample_rate);

// Sample fetch / interpolation (voice_engine_render_fetch.cpp).
void  VoiceRenderFetch_InitSqrtLut();
float SampleAtLinear(const Sample* s, float pos, bool wrap_end);
float SampleAtLinearRegion(const Sample* s,
                           float pos,
                           uint32_t start,
                           uint32_t end,
                           bool loop_enabled,
                           uint32_t loop_start,
                           uint32_t loop_end);

uint32_t ComputeLoopSeamCrossfadeFrames(uint32_t start, uint32_t end, float amount);
float ComputeLoopSeamCrossfadeWeight(float mix, float shape, bool fade_in);
float SampleAtLoopSeamCrossfade(const Sample* s,
                                float pos,
                                uint32_t start,
                                uint32_t end,
                                uint32_t seam_frames,
                                float shape,
                                float sample_rate,
                                bool& used_xfade);

float VoiceRenderFetch_VoiceStream(const Sample* sample,
                                   float pos,
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
