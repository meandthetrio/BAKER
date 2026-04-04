#include "audio_engine.h"
#include <cmath>
#include <cstring>

// Large stereo delay lines live in SDRAM (see sd_sample_pool.cpp).
ADSR2_SECTION(".sdram_bss") ADSR2_ALIGN32 static float g_delay_buf_L[AudioEngine::kDelayMaxSamples];
ADSR2_SECTION(".sdram_bss") ADSR2_ALIGN32 static float g_delay_buf_R[AudioEngine::kDelayMaxSamples];

inline float AudioEngine::SoftClip(float x)
{
    const float ax = std::fabs(x);
    return x / (1.0f + ax);
}

// ---- Delay ----
void AudioEngine::DelayClear_()
{
    std::memset(g_delay_buf_L, 0, sizeof(g_delay_buf_L));
    std::memset(g_delay_buf_R, 0, sizeof(g_delay_buf_R));
    delay_wr_ = 0;
    for(auto& b : delay_mid_hp_)
        b.Reset();
    for(auto& b : delay_mid_lp_)
        b.Reset();
}

void AudioEngine::DelayProcess_(float dryL, float dryR, float mix, bool feed_input, size_t len_l,
                                size_t len_r, float fb, float& outL, float& outR)
{
    const float inL = feed_input ? dryL : 0.0f;
    const float inR = feed_input ? dryR : 0.0f;

    const size_t wr  = delay_wr_;
    const size_t rdL = (wr + kDelayMaxSamples - len_l) % kDelayMaxSamples;
    const size_t rdR = (wr + kDelayMaxSamples - len_r) % kDelayMaxSamples;
    float        dL  = g_delay_buf_L[rdL];
    float        dR  = g_delay_buf_R[rdR];

    dL = delay_mid_lp_[0].Process(delay_mid_hp_[0].Process(dL));
    dR = delay_mid_lp_[1].Process(delay_mid_hp_[1].Process(dR));

    g_delay_buf_L[wr] = inL + dL * fb;
    g_delay_buf_R[wr] = inR + dR * fb;
    delay_wr_         = (wr + 1) % kDelayMaxSamples;

    outL = dryL + dL * mix;
    outR = dryR + dR * mix;
}

void AudioEngine::ReverbClear_()
{
    dattorro_.Init();
}

void AudioEngine::ReverbUpdateParamsDattorro_(const PerformParamsCurrent& p)
{
    float pre = p.reverb_pre;
    if(pre < 0.0f) pre = 0.0f;
    if(pre > 1.0f) pre = 1.0f;

    // Match the existing UI range expectation (0..120ms).
    constexpr float kPreDelayMaxMs = 120.0f;
    const float ms = pre * kPreDelayMaxMs;
    dattorro_.SetPredelay(ms);

    float damp = p.reverb_damp;
    if(damp < 0.0f) damp = 0.0f;
    if(damp > 1.0f) damp = 1.0f;
    dattorro_.SetDamping(damp);

    float decay = p.reverb_decay;
    if(decay < 0.0f) decay = 0.0f;
    if(decay > 1.0f) decay = 1.0f;
    dattorro_.SetDecay(decay);

    float mod = p.reverb_mod;
    if(mod < 0.0f) mod = 0.0f;
    if(mod > 1.0f) mod = 1.0f;

    // Use SetWetDry as “wet gain” inside Dattorro, but keep the external wet mix
    // behavior in AudioEngine (wet is extracted as out - in).
    // Keep internal wet gain fixed at 1.0; mod is applied inside Dattorro.
    dattorro_.SetWetDry(1.0f);

    // `mod` is wet Mid/Side stereo width inside DattorroReverb; predelay wobble is fixed.
    dattorro_.SetMod(mod);
}

void AudioEngine::ReverbProcessDattorro_(float inL, float inR, bool feed_input,
                                        float& wetL, float& wetR)
{
    float outL = 0.0f;
    float outR = 0.0f;
    if(feed_input)
    {
        dattorro_.Process(inL, inR, outL, outR);
        wetL = outL - inL;
        wetR = outR - inR;
    }
    else
    {
        // Tail: keep processing with zero input to let the internal tank decay.
        dattorro_.Process(0.0f, 0.0f, outL, outR);
        wetL = outL;
        wetR = outR;
    }
}

// ---- Engine ----
void AudioEngine::Init(float sample_rate, size_t block_size)
{
    sample_rate_ = sample_rate;
    block_size_  = block_size;

    DelayClear_();

    // Reverb init/clear
    dattorro_.Init();

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

    ReverbUpdateParamsDattorro_(p);

    // Delay lengths + feedback (smoothed params, constant within block)
    float dt = p.delay_time;
    if(dt < 0.0f)
        dt = 0.0f;
    else if(dt > 1.0f)
        dt = 1.0f;
    float sp = p.delay_spread;
    if(sp < 0.0f)
        sp = 0.0f;
    else if(sp > 1.0f)
        sp = 1.0f;
    const float t_ms      = dt * kDelayTimeMaxMs;
    const float t_samps_f = t_ms * sample_rate_ * (1.0f / 1000.0f);
    size_t      len_r     = (size_t)(t_samps_f + 0.5f);
    if(len_r < 1)
        len_r = 1;
    if(len_r >= kDelayMaxSamples)
        len_r = kDelayMaxSamples - 1;
    size_t len_l = (size_t)(t_samps_f * (1.0f - sp) + 0.5f);
    if(len_l < 1)
        len_l = 1;
    if(len_l > len_r)
        len_l = len_r;

    float dfb = p.delay_feedback;
    if(dfb < 0.0f)
        dfb = 0.0f;
    else if(dfb > 1.0f)
        dfb = 1.0f;
    static constexpr float kDelayFeedbackMax = 0.97f;
    const float            delay_fb          = dfb * kDelayFeedbackMax;

    float mid = p.delay_mid;
    if(mid < 0.0f)
        mid = 0.0f;
    else if(mid > 1.0f)
        mid = 1.0f;
    const float fc_hp = 20.0f + mid * (400.0f - 20.0f);
    const float fc_lp = 20000.0f + mid * (800.0f - 20000.0f);
    const float nyq   = 0.5f * sample_rate_;
    float       fc_lp_c = fc_lp;
    float       fc_hp_c = fc_hp;
    if(fc_lp_c > nyq * 0.49f)
        fc_lp_c = nyq * 0.49f;
    if(fc_hp_c < 20.0f)
        fc_hp_c = 20.0f;
    if(fc_hp_c >= fc_lp_c * 0.99f)
        fc_hp_c = fc_lp_c * 0.25f;

    static constexpr float kButterQ = 0.7071067811865476f;
    for(int ch = 0; ch < 2; ++ch)
    {
        delay_mid_hp_[ch].SetHighpass(fc_hp_c, sample_rate_, kButterQ);
        delay_mid_lp_[ch].SetLowpass(fc_lp_c, sample_rate_, kButterQ);
    }

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
                        DelayProcess_(l, r, mix, feed, len_l, len_r, delay_fb, dl, dr);

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
                        ReverbProcessDattorro_(l, r, feed, wetL, wetR);

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
