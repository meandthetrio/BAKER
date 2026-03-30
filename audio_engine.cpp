#include "audio_engine.h"
#include <cmath>
#include <cstring>

ADSR2_SRAM ADSR2_ALIGN32 static float g_delay_buf[AudioEngine::kDelayMaxSamples];
ADSR2_SRAM ADSR2_ALIGN32 static float g_reverb_pre[AudioEngine::kReverbPreMaxSamples];
ADSR2_SRAM ADSR2_ALIGN32 static float g_reverb_diff1[AudioEngine::kReverbDiff1Len];
ADSR2_SRAM ADSR2_ALIGN32 static float g_reverb_diff2[AudioEngine::kReverbDiff2Len];
ADSR2_SRAM ADSR2_ALIGN32 static float g_reverb_diff3[AudioEngine::kReverbDiff3Len];
ADSR2_SRAM ADSR2_ALIGN32 static float g_reverb_tank1[AudioEngine::kReverbTank1Len];
ADSR2_SRAM ADSR2_ALIGN32 static float g_reverb_tank2[AudioEngine::kReverbTank2Len];
ADSR2_SRAM ADSR2_ALIGN32 static float g_reverb_tank3[AudioEngine::kReverbTank3Len];
ADSR2_SRAM ADSR2_ALIGN32 static float g_reverb_tank4[AudioEngine::kReverbTank4Len];
ADSR2_SRAM ADSR2_ALIGN32 static float g_reverb_tank5[AudioEngine::kReverbTank5Len];
ADSR2_SRAM ADSR2_ALIGN32 static float g_reverb_tank6[AudioEngine::kReverbTank6Len];
ADSR2_SRAM ADSR2_ALIGN32 static float g_reverb_tank7[AudioEngine::kReverbTank7Len];
ADSR2_SRAM ADSR2_ALIGN32 static float g_reverb_tank8[AudioEngine::kReverbTank8Len];

inline float AudioEngine::SoftClip(float x)
{
    const float ax = std::fabs(x);
    return x / (1.0f + ax);
}

static inline void Hadamard8_(const float in[AudioEngine::kReverbTankCount],
                              float out[AudioEngine::kReverbTankCount])
{
    const float a0 = in[0] + in[1];
    const float a1 = in[0] - in[1];
    const float a2 = in[2] + in[3];
    const float a3 = in[2] - in[3];
    const float a4 = in[4] + in[5];
    const float a5 = in[4] - in[5];
    const float a6 = in[6] + in[7];
    const float a7 = in[6] - in[7];

    const float b0 = a0 + a2;
    const float b1 = a1 + a3;
    const float b2 = a0 - a2;
    const float b3 = a1 - a3;
    const float b4 = a4 + a6;
    const float b5 = a5 + a7;
    const float b6 = a4 - a6;
    const float b7 = a5 - a7;

    constexpr float kScale = 0.35355339059f; // 1 / sqrt(8)
    out[0] = kScale * (b0 + b4);
    out[1] = kScale * (b1 + b5);
    out[2] = kScale * (b2 + b6);
    out[3] = kScale * (b3 + b7);
    out[4] = kScale * (b0 - b4);
    out[5] = kScale * (b1 - b5);
    out[6] = kScale * (b2 - b6);
    out[7] = kScale * (b3 - b7);
}

// ---- Delay ----
void AudioEngine::DelayClear_()
{
    std::memset(g_delay_buf, 0, sizeof(g_delay_buf));
    delay_wr_ = 0;
}

void AudioEngine::DelayProcess_(float dryL, float dryR, float mix, bool feed_input,
                                float& outL, float& outR)
{
    const float in_mono = feed_input ? (0.5f * (dryL + dryR)) : 0.0f;

    const size_t rd = (delay_wr_ + kDelayMaxSamples - delay_len_) % kDelayMaxSamples;
    const float  d  = g_delay_buf[rd];

    g_delay_buf[delay_wr_] = in_mono + d * delay_fb_;
    delay_wr_ = (delay_wr_ + 1) % kDelayMaxSamples;

    outL = dryL + d * mix;
    outR = dryR + d * mix;
}

// ---- Reverb primitives ----
void AudioEngine::Diffuser::Clear()
{
    if(buf && len)
        std::memset(buf, 0, len * sizeof(float));
    idx = 0;
}

float AudioEngine::Diffuser::Process(float in)
{
    // Classic lightweight allpass diffuser.
    const float b = buf[idx];
    const float y = -in + b;
    buf[idx] = in + y * g;
    idx++;
    if(idx >= len) idx = 0;
    return y;
}

void AudioEngine::TankLine::Clear()
{
    if(buf && len)
        std::memset(buf, 0, len * sizeof(float));
    idx    = 0;
    damp_z = 0.0f;
}

float AudioEngine::TankLine::Read() const
{
    return (buf && len) ? buf[idx] : 0.0f;
}

void AudioEngine::TankLine::WriteAdvance(float in)
{
    if(!buf || !len)
        return;

    buf[idx] = in;
    idx++;
    if(idx >= len) idx = 0;
}

void AudioEngine::ReverbInit_()
{
    reverb_diffusers_[0].Init(g_reverb_diff1, kReverbDiff1Len);
    reverb_diffusers_[1].Init(g_reverb_diff2, kReverbDiff2Len);
    reverb_diffusers_[2].Init(g_reverb_diff3, kReverbDiff3Len);
    reverb_tank_[0].Init(g_reverb_tank1, kReverbTank1Len);
    reverb_tank_[1].Init(g_reverb_tank2, kReverbTank2Len);
    reverb_tank_[2].Init(g_reverb_tank3, kReverbTank3Len);
    reverb_tank_[3].Init(g_reverb_tank4, kReverbTank4Len);
    reverb_tank_[4].Init(g_reverb_tank5, kReverbTank5Len);
    reverb_tank_[5].Init(g_reverb_tank6, kReverbTank6Len);
    reverb_tank_[6].Init(g_reverb_tank7, kReverbTank7Len);
    reverb_tank_[7].Init(g_reverb_tank8, kReverbTank8Len);

    ReverbClear_();
}

void AudioEngine::ReverbClear_()
{
    std::memset(g_reverb_pre, 0, sizeof(g_reverb_pre));
    reverb_pre_wr_ = 0;

    reverb_diffusers_[0].g = 0.68f;
    reverb_diffusers_[1].g = 0.56f;
    reverb_diffusers_[2].g = 0.47f;
    for(int i = 0; i < 3; ++i)
        reverb_diffusers_[i].Clear();

    for(size_t i = 0; i < kReverbTankCount; ++i)
        reverb_tank_[i].Clear();
}

void AudioEngine::ReverbUpdateParams_(const PerformParamsCurrent& p)
{
    // Phase A keeps the existing reverse control published/UI-visible,
    // but the DSP path intentionally ignores it until a later phase.
    float pre = p.reverb_pre;
    if(pre < 0.0f) pre = 0.0f;
    if(pre > 1.0f) pre = 1.0f;

    float damp = p.reverb_damp;
    if(damp < 0.0f) damp = 0.0f;
    if(damp > 1.0f) damp = 1.0f;

    float decay = p.reverb_decay;
    if(decay < 0.0f) decay = 0.0f;
    if(decay > 1.0f) decay = 1.0f;

    constexpr float kPreDelayMaxMs = 120.0f;
    size_t want = static_cast<size_t>(sample_rate_ * (kPreDelayMaxMs * 0.001f) * pre);
    if(want >= kReverbPreMaxSamples)
        want = kReverbPreMaxSamples - 1;
    reverb_pre_len_ = (want > 0u) ? want : 1u;

    reverb_decay_gain_ = 0.30f + decay * 0.64f;

    const float damp_shaped = damp * damp * (3.0f - 2.0f * damp);
    const float brightness  = 1.0f - damp_shaped;
    constexpr float kMinDampCutoffHz = 950.0f;
    constexpr float kMaxDampCutoffHz = 14000.0f;
    const float cutoff_hz = kMinDampCutoffHz
                            * std::pow(kMaxDampCutoffHz / kMinDampCutoffHz, brightness);
    reverb_damp_coeff_ = 1.0f - std::exp(-2.0f * 3.14159265f * cutoff_hz / sample_rate_);
    if(reverb_damp_coeff_ < 0.05f)
        reverb_damp_coeff_ = 0.05f;
    else if(reverb_damp_coeff_ > 0.88f)
        reverb_damp_coeff_ = 0.88f;
}

void AudioEngine::ReverbProcess_(float inL, float inR, bool feed_input,
                                 float& wetL, float& wetR)
{
    const float x = feed_input ? (0.5f * (inL + inR)) : 0.0f;

    const size_t pre_rd = (reverb_pre_wr_ + kReverbPreMaxSamples - reverb_pre_len_) % kReverbPreMaxSamples;
    const float  pre    = g_reverb_pre[pre_rd];
    g_reverb_pre[reverb_pre_wr_] = x;
    reverb_pre_wr_++;
    if(reverb_pre_wr_ >= kReverbPreMaxSamples) reverb_pre_wr_ = 0;

    const float d1 = reverb_diffusers_[0].Process(pre);
    const float d2 = reverb_diffusers_[1].Process(d1);
    const float d3 = reverb_diffusers_[2].Process(0.75f * d2 + 0.25f * d1);

    float y[kReverbTankCount];
    float damped[kReverbTankCount];
    for(size_t i = 0; i < kReverbTankCount; ++i)
    {
        y[i] = reverb_tank_[i].Read();
        const float z = reverb_tank_[i].damp_z
                        + reverb_damp_coeff_ * (y[i] - reverb_tank_[i].damp_z);
        reverb_tank_[i].damp_z = z;
        damped[i]              = z;
    }

    float fb[kReverbTankCount];
    Hadamard8_(damped, fb);

    const float scatter_a = 0.55f * d3 + 0.30f * d2 + 0.15f * d1;
    const float scatter_b = 0.72f * d3 - 0.28f * d2;
    const float scatter_c = 0.50f * d2 - 0.50f * d1;
    const float scatter_d = 0.82f * d3 + 0.18f * d1;

    constexpr float kInputScale = 0.115f;
    const float inputs[kReverbTankCount] = {
        kInputScale * ( scatter_a + 0.15f * scatter_c),
        kInputScale * ( scatter_b - 0.10f * scatter_a),
        kInputScale * ( scatter_c + 0.10f * scatter_d),
        kInputScale * ( scatter_d - 0.15f * scatter_b),
        kInputScale * (-scatter_a + 0.15f * scatter_d),
        kInputScale * (-scatter_c - 0.10f * scatter_a),
        kInputScale * (-scatter_d + 0.10f * scatter_b),
        kInputScale * (-scatter_b - 0.15f * scatter_c),
    };
    for(size_t i = 0; i < kReverbTankCount; ++i)
        reverb_tank_[i].WriteAdvance(inputs[i] + reverb_decay_gain_ * fb[i]);

    float voiced[kReverbTankCount];
    for(size_t i = 0; i < kReverbTankCount; ++i)
        voiced[i] = 0.18f * y[i] + 0.82f * damped[i];

    constexpr float kOutputScale = 0.25f;
    const float earlyL = 0.045f * (0.55f * d2 + 0.45f * d3);
    const float earlyR = 0.045f * (0.55f * d3 - 0.45f * d2);
    const float lateL  = kOutputScale
                        * ( voiced[0] + voiced[1] - voiced[2] - voiced[3]
                          + voiced[4] + voiced[5] - voiced[6] - voiced[7]);
    const float lateR  = kOutputScale
                        * ( voiced[0] - voiced[1] - voiced[2] + voiced[3]
                          + voiced[4] - voiced[5] - voiced[6] + voiced[7]);

    wetL = earlyL + lateL;
    wetR = earlyR + lateR;
}

// ---- Engine ----
void AudioEngine::Init(float sample_rate, size_t block_size)
{
    sample_rate_ = sample_rate;
    block_size_  = block_size;

    // Delay length ~200ms (kept small until we decide SDRAM placement)
    const float delay_ms = 200.0f;
    size_t want = (size_t)(sample_rate_ * (delay_ms * 0.001f));
    if(want < 1) want = 1;
    if(want >= kDelayMaxSamples) want = kDelayMaxSamples - 1;
    delay_len_ = want;

    DelayClear_();

    // Reverb init/clear
    ReverbInit_();

    delay_active_  = false;
    delay_tailing_ = false;
    delay_tail_blocks_left_ = 0;
    delay_quiet_blocks_     = 0;
    delay_tail_mix_         = 0.0f;

    reverb_active_  = false;
    reverb_tailing_ = false;
    reverb_tail_blocks_left_ = 0;
    reverb_quiet_blocks_     = 0;
    reverb_tail_mix_         = 0.0f;
}

void AudioEngine::ProcessBlock(const float* inL,
                               const float* inR,
                               float* outL,
                               float* outR,
                               size_t size,
                               const PerformParamsCurrent& p)
{
    // Master level can exceed unity for user "BOOST" (e.g. 0..2.0).
    // Clamp here as a last line of defense (UI/params should also clamp).
    float level = p.master_level;
    if(level < 0.0f) level = 0.0f;
    if(level > 2.0f) level = 2.0f;

    // BOOST-bypass ramp: from UNITY (<=1.0) to "bypass poly headroom" at 2.0
    // This cancels the conservative per-voice gain used for safe polyphony, so single-sample preview can get loud.
    // voice_engine.cpp uses: static constexpr float kVoiceAmpScale = 0.15f
    static constexpr float kPolyHeadroomScale = 0.15f;
    static constexpr float kBypassGain = 1.0f / kPolyHeadroomScale;

    float t_boost = 0.0f;
    if(level > 1.0f)
    {
        t_boost = level - 1.0f;
        if(t_boost < 0.0f) t_boost = 0.0f;
        if(t_boost > 1.0f) t_boost = 1.0f;
    }

    const float bypass_comp = 1.0f + t_boost * (kBypassGain - 1.0f);

    // ---- Delay ON/OFF -> active/tail ----
    if(p.delay_on)
    {
        if(!delay_active_ && !delay_tailing_)
            DelayClear_();

        delay_active_  = true;
        delay_tailing_ = false;
        delay_quiet_blocks_ = 0;
    }
    else
    {
        if(delay_active_ && !delay_tailing_)
        {
            delay_tailing_ = true;
            delay_tail_blocks_left_ = kDelayTailMaxBlocks;
            delay_quiet_blocks_     = 0;
            delay_tail_mix_         = p.delay_mix;
        }
    }

    // ---- Reverb ON/OFF -> active/tail ----
    if(p.reverb_on)
    {
        if(!reverb_active_ && !reverb_tailing_)
            ReverbClear_();

        reverb_active_  = true;
        reverb_tailing_ = false;
        reverb_quiet_blocks_ = 0;
    }
    else
    {
        if(reverb_active_ && !reverb_tailing_)
        {
            reverb_tailing_ = true;
            reverb_tail_blocks_left_ = kReverbTailMaxBlocks;
            reverb_quiet_blocks_     = 0;
            reverb_tail_mix_         = p.reverb_mix;
        }
    }

    // ---- SAT (hard bypass) ----
    const bool  sat_run = (p.sat_on && p.sat_drive >= 0.0001f);
    const float pre     = 1.0f + p.sat_drive * 10.0f;

    const float delay_mix_on  = p.delay_mix;
    const float reverb_mix_on = p.reverb_mix;

    ReverbUpdateParams_(p);

    float delay_wet_peak  = 0.0f;
    float reverb_wet_peak = 0.0f;

    for(size_t i = 0; i < size; i++)
    {
        float l = inL[i];
        float r = inR[i];

        for(uint8_t stage_idx = 0; stage_idx < 4; ++stage_idx)
        {
            const uint8_t fx = p.fx_order[stage_idx];
            switch(fx)
            {
                case 0: // SAT
                    if(sat_run)
                    {
                        l = SoftClip(l * pre);
                        r = SoftClip(r * pre);
                    }
                    break;
                case 1: // MOD (placeholder in current CuzEngine audio chain)
                    break;
                case 2: // DELAY
                    if(delay_active_ || delay_tailing_)
                    {
                        const bool  feed = p.delay_on;
                        const float mix  = p.delay_on ? delay_mix_on : delay_tail_mix_;

                        float dl, dr;
                        DelayProcess_(l, r, mix, feed, dl, dr);

                        delay_wet_peak = std::fmax(delay_wet_peak, std::fabs(dl - l));
                        delay_wet_peak = std::fmax(delay_wet_peak, std::fabs(dr - r));

                        l = dl;
                        r = dr;
                    }
                    break;
                case 3: // REVERB
                    if(reverb_active_ || reverb_tailing_)
                    {
                        const bool  feed = p.reverb_on;
                        const float mix  = p.reverb_on ? reverb_mix_on : reverb_tail_mix_;

                        float wetL, wetR;
                        ReverbProcess_(l, r, feed, wetL, wetR);

                        const float rl = l + wetL * mix;
                        const float rr = r + wetR * mix;

                        reverb_wet_peak = std::fmax(reverb_wet_peak, std::fabs(rl - l));
                        reverb_wet_peak = std::fmax(reverb_wet_peak, std::fabs(rr - r));

                        l = rl;
                        r = rr;
                    }
                    break;
                default:
                    break;
            }
        }

        // Apply master level, then (only when BOOST is in play) soft-clip at the very end
        // to avoid hard digital clipping.
        float ol = l * level * bypass_comp;
        float or_ = r * level * bypass_comp;

        // Final safety: once we are boosting, prevent hard digital clipping at the DAC
        if(level > 1.0001f)
        {
            ol  = SoftClip(ol);
            or_ = SoftClip(or_);
        }
        outL[i] = ol;
        outR[i] = or_;
    }

    // ---- Delay tail bookkeeping ----
    if(delay_tailing_)
    {
        if(delay_wet_peak < kTailSilenceThresh) delay_quiet_blocks_++;
        else delay_quiet_blocks_ = 0;

        if(delay_tail_blocks_left_ > 0) delay_tail_blocks_left_--;

        if(delay_tail_blocks_left_ == 0 || delay_quiet_blocks_ >= kQuietBlocksToStop)
        {
            delay_tailing_ = false;
            delay_active_  = false;
            DelayClear_();
        }
    }
    else if(!p.delay_on)
    {
        delay_active_ = false;
    }

    // ---- Reverb tail bookkeeping ----
    if(reverb_tailing_)
    {
        if(reverb_wet_peak < kTailSilenceThresh) reverb_quiet_blocks_++;
        else reverb_quiet_blocks_ = 0;

        if(reverb_tail_blocks_left_ > 0) reverb_tail_blocks_left_--;

        if(reverb_tail_blocks_left_ == 0 || reverb_quiet_blocks_ >= kQuietBlocksToStop)
        {
            reverb_tailing_ = false;
            reverb_active_  = false;
            ReverbClear_();
        }
    }
    else if(!p.reverb_on)
    {
        reverb_active_ = false;
    }
}
