#pragma once

#include "voice_engine.h"

float ComputeRatio(uint8_t note, uint8_t root_key);
float ComputeRatioFromSemitoneDelta(float semitones);
float ComputeFadeStepMs(float sample_rate, float fade_ms);

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
