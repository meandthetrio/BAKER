#pragma once

#include "voice_engine.h"

float ComputeRatio(uint8_t note, uint8_t root_key);
float ComputeFadeStepMs(float sample_rate, float fade_ms);
float ComputeFadeStep(float sample_rate);

void InitEnvelope(EnvStage& stage,
                  float&    level,
                  float&    a_step,
                  float&    d_step,
                  float&    r_step,
                  float&    sustain,
                  float     attack_ms,
                  float     decay_ms,
                  float     sustain_level,
                  float     release_ms,
                  float     sample_rate);

void SetEnvelopeRelease(EnvStage& stage,
                        float&    level,
                        float&    r_step,
                        float     release_ms,
                        float     sample_rate);

void StepEnvelope(EnvStage& stage,
                  float&    level,
                  float     a_step,
                  float     d_step,
                  float     sustain,
                  float     r_step);

float ComputeLoopBoundaryFade(float pos, uint32_t start, uint32_t end, float sample_rate);

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
