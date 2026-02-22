#include "audio_engine.h"
#include <cmath>
#include <cstring>

ADSR2_SRAM ADSR2_ALIGN32 static float g_delay_buf[AudioEngine::kDelayMaxSamples];
ADSR2_SRAM ADSR2_ALIGN32 static float g_comb1L[AudioEngine::kC1L], g_comb2L[AudioEngine::kC2L],
    g_comb3L[AudioEngine::kC3L], g_comb4L[AudioEngine::kC4L];
ADSR2_SRAM ADSR2_ALIGN32 static float g_comb1R[AudioEngine::kC1R], g_comb2R[AudioEngine::kC2R],
    g_comb3R[AudioEngine::kC3R], g_comb4R[AudioEngine::kC4R];
ADSR2_SRAM ADSR2_ALIGN32 static float g_ap1L[AudioEngine::kA1L], g_ap2L[AudioEngine::kA2L];
ADSR2_SRAM ADSR2_ALIGN32 static float g_ap1R[AudioEngine::kA1R], g_ap2R[AudioEngine::kA2R];

inline float AudioEngine::SoftClip(float x)
{
    const float ax = std::fabs(x);
    return x / (1.0f + ax);
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
void AudioEngine::Comb::Clear()
{
    if(buf && len)
        std::memset(buf, 0, len * sizeof(float));
    idx = 0;
}

float AudioEngine::Comb::Process(float in)
{
    // y = buf[idx]; buf[idx] = in + y*fb
    const float y = buf[idx];
    buf[idx] = in + y * fb;
    idx++;
    if(idx >= len) idx = 0;
    return y;
}

void AudioEngine::Allpass::Clear()
{
    if(buf && len)
        std::memset(buf, 0, len * sizeof(float));
    idx = 0;
}

float AudioEngine::Allpass::Process(float in)
{
    // classic allpass:
    // y = -in + buf[idx]
    // buf[idx] = in + y*g
    const float b = buf[idx];
    const float y = -in + b;
    buf[idx] = in + y * g;
    idx++;
    if(idx >= len) idx = 0;
    return y;
}

void AudioEngine::ReverbInit_()
{
    combL_[0].Init(g_comb1L, kC1L); combL_[1].Init(g_comb2L, kC2L);
    combL_[2].Init(g_comb3L, kC3L); combL_[3].Init(g_comb4L, kC4L);

    combR_[0].Init(g_comb1R, kC1R); combR_[1].Init(g_comb2R, kC2R);
    combR_[2].Init(g_comb3R, kC3R); combR_[3].Init(g_comb4R, kC4R);

    apL_[0].Init(g_ap1L, kA1L); apL_[1].Init(g_ap2L, kA2L);
    apR_[0].Init(g_ap1R, kA1R); apR_[1].Init(g_ap2R, kA2R);

    ReverbClear_();
}

void AudioEngine::ReverbClear_()
{
    for(int i = 0; i < 4; i++)
    {
        combL_[i].fb = reverb_fb_;
        combR_[i].fb = reverb_fb_;
        combL_[i].Clear();
        combR_[i].Clear();
    }
    for(int i = 0; i < 2; i++)
    {
        apL_[i].g = 0.5f;
        apR_[i].g = 0.5f;
        apL_[i].Clear();
        apR_[i].Clear();
    }
}

// feed_input=false => drive the reverb with silence (tail mode)
void AudioEngine::ReverbProcess_(float inL, float inR, bool feed_input,
                                 float& wetL, float& wetR)
{
    const float xL = feed_input ? inL : 0.0f;
    const float xR = feed_input ? inR : 0.0f;

    // very small input diffusion (mix channels slightly)
    const float in_mono = 0.5f * (xL + xR);
    const float driveL  = 0.7f * xL + 0.3f * in_mono;
    const float driveR  = 0.7f * xR + 0.3f * in_mono;

    // comb sum
    float cL = 0.0f, cR = 0.0f;
    for(int i = 0; i < 4; i++)
    {
        combL_[i].fb = reverb_fb_;
        combR_[i].fb = reverb_fb_;
        cL += combL_[i].Process(driveL);
        cR += combR_[i].Process(driveR);
    }
    cL *= 0.25f;
    cR *= 0.25f;

    // allpass chain
    float yL = apL_[0].Process(cL);
    yL = apL_[1].Process(yL);

    float yR = apR_[0].Process(cR);
    yR = apR_[1].Process(yR);

    wetL = yL;
    wetR = yR;
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
    const float level = p.master_level;

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

    float delay_wet_peak  = 0.0f;
    float reverb_wet_peak = 0.0f;

    for(size_t i = 0; i < size; i++)
    {
        float l = inL[i];
        float r = inR[i];

        // SAT
        if(sat_run)
        {
            l = SoftClip(l * pre);
            r = SoftClip(r * pre);
        }

        // DELAY (hard bypass + tail)
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

        // REVERB (hard bypass + tail)
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

        outL[i] = l * level;
        outR[i] = r * level;
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
