#pragma once

#include "voice_engine.h"

float ComputeRatio(uint8_t note, uint8_t root_key);
float ComputeRatioFromSemitoneDelta(float semitones);
float ComputeFadeStepMs(float sample_rate, float fade_ms);

// --- Exponential (analog-style) ADSR shaping ----------------------------------
// The loop envelope is an EarLevel-style one-pole: each stage moves the level a
// fixed fraction of the remaining distance to an overshoot target every sample
// (level += (target - level) * g). That gives the natural RC-capacitor curve —
// attack rises fast then eases into the peak; decay/release fall fast then taper.
// The a_step/d_step/r_step fields hold the per-stage gain g (1 - coef), computed
// in InitEnvelope/SetEnvelopeRelease. Stage advance still triggers when the level
// crosses the stage's nominal end (1.0 / sustain / 0), so the *_ms times keep
// their meaning (time to reach that crossing) and the overshoot is never heard.
//
// kEnvAttackTarget = 1 + ratioA (ratioA = 0.3 -> a gentle, musical attack curve).
// kEnvDecRelOvershoot = ratioDR (0.0001 -> a strongly exponential decay/release).
// The matching natural-log constants (used to turn a sample count into g) are
// kEnvAttackLn = ln(1.3/0.3) and kEnvDecRelLn = ln(1.0001/0.0001).
static constexpr float kEnvAttackTarget    = 1.3f;
static constexpr float kEnvDecRelOvershoot = 0.0001f;
static constexpr float kEnvAttackLn        = 1.46633707f;
static constexpr float kEnvDecRelLn        = 9.21044037f;

// --- exp vs log curve selection -----------------------------------------------
// Attack and release can each be exponential (concave: fast then slow — the
// analog default) or logarithmic (convex: slow then fast — holds louder/longer,
// so it perceptually fills more of the set time). Both shapes use the SAME
// one-pole update `level += (target - level) * g`; the only difference is the
// target side and the sign of g:
//   exp attack : target = +1.3 , g > 0   (approach from below, decelerating)
//   log attack : target = -0.3 , g < 0   (growth away from -ratioA, accelerating)
//   exp release: target = -0.0001, g > 0
//   log release: target = +1.1   , g < 0  (holds near 1.0 then drops)
// So the SIGN of the stored a_step/r_step carries the mode (negative = log) and
// StepEnvelope derives the target from it — no extra per-voice state. g is
// computed as 1 - exp(-/+ ln/samples); the curve still crosses 1.0 / 0 at the
// configured sample count (symmetric to the exp calibration).
//
// Log release uses a much larger overshoot (ratioDR_log = 0.1) than exp's 0.0001:
// it starts at level 1.0 and barely moves at first, and with the tiny 0.0001
// overshoot the per-sample step underflows float precision near 1.0 at long
// times (>~1 s) and stalls. 0.1 keeps the step representable through the 4 s max
// and also gives a cleaner convex "hold then fall" shape. (Log attack starts at
// 0 where precision is fine, so it keeps ratioA = 0.3 / kEnvAttackLn.)
static constexpr float kEnvAttackTargetLog  = -0.3f;            // -ratioA
static constexpr float kEnvReleaseTargetExp = -kEnvDecRelOvershoot;
static constexpr float kEnvReleaseTargetLog = 1.1f;            // 1 + ratioDR_log
static constexpr float kEnvReleaseLnLog     = 2.39789527f;     // ln(1.1 / 0.1)

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
                  float     sample_rate,
                  bool      attack_log,
                  bool      release_log);

void SetEnvelopeRelease(EnvStage& stage,
                        float&    level,
                        float&    r_step,
                        float     release_ms,
                        float     sample_rate,
                        bool      release_log);

// Inlined in the header so the env pre-simulation loop in RenderNormalVoice_
// (48 iterations per voice per attack block) and the per-sample fast-envelope
// path don't pay a cross-TU `bl` per iteration. Bit-identical to the prior
// out-of-line version that lived in voice_engine_playback.cpp.
static inline void StepEnvelope(EnvStage& stage,
                                float&    level,
                                float     a_step,
                                float     d_step,
                                float     sustain,
                                float     r_step)
{
    // a_step/d_step/r_step carry the per-stage one-pole gain g (see header notes).
    // The sign of a_step/r_step selects exp (>=0) vs log (<0); the target follows.
    switch(stage)
    {
        case EnvStage::Attack:
        {
            const float a_t = (a_step >= 0.0f) ? kEnvAttackTarget : kEnvAttackTargetLog;
            level += (a_t - level) * a_step;
            if(level >= 1.0f)
            {
                level = 1.0f;
                stage = EnvStage::Decay;
            }
            break;
        }
        case EnvStage::Decay:
            level += ((sustain - kEnvDecRelOvershoot) - level) * d_step;
            if(level <= sustain)
            {
                level = sustain;
                stage = EnvStage::Sustain;
            }
            break;
        case EnvStage::Sustain:
            level = sustain;
            break;
        case EnvStage::Release:
        {
            const float r_t = (r_step >= 0.0f) ? kEnvReleaseTargetExp : kEnvReleaseTargetLog;
            level += (r_t - level) * r_step;
            if(level <= 0.0f)
            {
                level = 0.0f;
                stage = EnvStage::Off;
            }
            break;
        }
        case EnvStage::Off:
        default:
            level = 0.0f;
            break;
    }
}

float ComputeLoopBoundaryFade(float pos, uint32_t start, uint32_t end, float sample_rate);
