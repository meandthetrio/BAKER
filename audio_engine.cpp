#include "audio_engine.h"
#include "app_state_diagnostics.h"
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

// ---- Per-block FX processors ----
//
// Each of these walks the whole block in place. The per-sample dispatch cost
// (switch on fx_order[stage]) from the old ProcessBlock is now paid once per
// stage per block instead of once per sample per stage. DSP is identical.

void AudioEngine::ProcessSatBlock_(float* L, float* R, size_t n, float pre)
{
    uint32_t hit_count = 0u;
    for(size_t i = 0; i < n; ++i)
    {
        const float pre_l = L[i] * pre;
        const float pre_r = R[i] * pre;
        if(std::fabs(pre_l) > 1.0f)
            ++hit_count;
        if(std::fabs(pre_r) > 1.0f)
            ++hit_count;
        L[i] = SoftClip(pre_l);
        R[i] = SoftClip(pre_r);
    }
    if(diagnostics_ && hit_count > 0u)
        diagnostics_->sat_softclip_hits.fetch_add(hit_count, std::memory_order_relaxed);
}

void AudioEngine::ProcessEqBlock_(float* L, float* R, size_t n, float eq_mix)
{
    tilt_eq_.ProcessBlock(L, R, n, eq_mix);
}

void AudioEngine::ProcessDelayBlock_(float* L, float* R, size_t n,
                                     const PerformParamsCurrent& p,
                                     size_t len_l, size_t len_r, float fb,
                                     float& wet_peak)
{
    const bool  feed = p.delay_on;
    const float mix  = p.delay_on ? p.delay_mix : delay_tail_mix_;
    const bool  mix_mode = (p.delay_fader_mode == kDelayFaderModeMix);
    float       peak = wet_peak;

    // Hoist the write index into a local for the duration of the block so the
    // compiler can keep it in a register; write it back to the member once.
    // Modulo arithmetic preserved exactly.
    size_t wr = delay_wr_;
    for(size_t i = 0; i < n; ++i)
    {
        const float l   = L[i];
        const float r   = R[i];
        const float inL = feed ? l : 0.0f;
        const float inR = feed ? r : 0.0f;

        const size_t rdL = (wr + kDelayMaxSamples - len_l) % kDelayMaxSamples;
        const size_t rdR = (wr + kDelayMaxSamples - len_r) % kDelayMaxSamples;
        const float  dL  = g_delay_buf_L[rdL];
        const float  dR  = g_delay_buf_R[rdR];

        g_delay_buf_L[wr] = inL + dL * fb;
        g_delay_buf_R[wr] = inR + dR * fb;
        wr                = (wr + 1) % kDelayMaxSamples;

        float dl_out = l + dL * mix;
        float dr_out = r + dR * mix;
        if(feed && mix_mode)
        {
            dl_out = l * (1.0f - mix) + dL * mix;
            dr_out = r * (1.0f - mix) + dR * mix;
        }

        const float abs_dl = std::fabs(dl_out - l);
        const float abs_dr = std::fabs(dr_out - r);
        if(abs_dl > peak) peak = abs_dl;
        if(abs_dr > peak) peak = abs_dr;

        L[i] = dl_out;
        R[i] = dr_out;
    }
    delay_wr_ = wr;
    wet_peak  = peak;
}

void AudioEngine::ProcessReverbBlock_(float* L, float* R, size_t n,
                                      const PerformParamsCurrent& p,
                                      float& wet_peak)
{
    const bool  feed = p.reverb_on;
    const float mix  = p.reverb_on ? p.reverb_mix : reverb_tail_mix_;
    const bool  mix_mode = (p.reverb_fader_mode == kReverbFaderModeMix);
    float       peak = wet_peak;

    // Fixed-size stack scratch buffers sized to the hardware audio block
    // (48 samples on this target). 2 buffers * 48 floats = 384 bytes total.
    constexpr size_t kReverbScratchMax = 48;
    float            tmpL[kReverbScratchMax];
    float            tmpR[kReverbScratchMax];

    if(feed)
    {
        // Live input path: DattorroReverb::ProcessBlock writes in + wet*out_gain,
        // so wet = out - in (matches the old per-sample ReverbProcessDattorro_).
        dattorro_.ProcessBlock(L, R, tmpL, tmpR, n);
        for(size_t i = 0; i < n; ++i)
        {
            const float l    = L[i];
            const float r    = R[i];
            const float wetL = tmpL[i] - l;
            const float wetR = tmpR[i] - r;

            float rl = l + wetL * mix;
            float rr = r + wetR * mix;
            if(mix_mode)
            {
                rl = l * (1.0f - mix) + wetL * mix;
                rr = r * (1.0f - mix) + wetR * mix;
            }

            const float abs_rl = std::fabs(rl - l);
            const float abs_rr = std::fabs(rr - r);
            if(abs_rl > peak) peak = abs_rl;
            if(abs_rr > peak) peak = abs_rr;

            L[i] = rl;
            R[i] = rr;
        }
    }
    else
    {
        // Tail path: feed zeros to let the tank decay. Matches the old
        // ReverbProcessDattorro_ behavior when feed_input == false, where
        // outL/outR are the full wet output (no dry component).
        float zL[kReverbScratchMax] = {0.0f};
        float zR[kReverbScratchMax] = {0.0f};
        dattorro_.ProcessBlock(zL, zR, tmpL, tmpR, n);
        for(size_t i = 0; i < n; ++i)
        {
            const float l    = L[i];
            const float r    = R[i];
            const float wetL = tmpL[i];
            const float wetR = tmpR[i];

            const float rl = l + wetL * mix;
            const float rr = r + wetR * mix;

            const float abs_rl = std::fabs(rl - l);
            const float abs_rr = std::fabs(rr - r);
            if(abs_rl > peak) peak = abs_rl;
            if(abs_rr > peak) peak = abs_rr;

            L[i] = rl;
            R[i] = rr;
        }
    }
    wet_peak = peak;
}

void AudioEngine::ApplyMasterBlock_(float* L, float* R, size_t n,
                                    float level, float bypass_comp)
{
    const float g       = level * bypass_comp;
    const bool  boosted = (level > 1.0001f);
    if(boosted)
    {
        uint32_t hit_count = 0u;
        for(size_t i = 0; i < n; ++i)
        {
            const float pre_l = L[i] * g;
            const float pre_r = R[i] * g;
            if(std::fabs(pre_l) > 1.0f)
                ++hit_count;
            if(std::fabs(pre_r) > 1.0f)
                ++hit_count;
            L[i] = SoftClip(pre_l);
            R[i] = SoftClip(pre_r);
        }
        if(diagnostics_ && hit_count > 0u)
            diagnostics_->master_softclip_hits.fetch_add(hit_count, std::memory_order_relaxed);
    }
    else
    {
        for(size_t i = 0; i < n; ++i)
        {
            L[i] = L[i] * g;
            R[i] = R[i] * g;
        }
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

    tilt_eq_.Reset();
    eq_run_prev_ = false;
}

void AudioEngine::ProcessBlock(const float* inL,
                               const float* inR,
                               float* outL,
                               float* outR,
                               size_t size,
                               const PerformParamsCurrent& p,
                               bool sd_wav_load_busy)
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

    if(!sd_wav_load_busy)
        ReverbUpdateParamsDattorro_(p);

    const bool eq_run = (p.eq_on && p.eq_mix > 1e-5f);
    if(eq_run && !eq_run_prev_)
        tilt_eq_.Reset();
    eq_run_prev_ = eq_run;
    if(eq_run)
    {
        float tilt = p.eq_tilt_db;
        if(tilt < -kTiltEqTiltMaxDb)
            tilt = -kTiltEqTiltMaxDb;
        else if(tilt > kTiltEqTiltMaxDb)
            tilt = kTiltEqTiltMaxDb;
        const float center_hz = TiltEq_CenterNormToHz(p.eq_center_norm);
        tilt_eq_.SetFromParams(center_hz, tilt, sample_rate_, p.eq_q);
    }

    // Delay L/R times + feedback (smoothed params, constant within block)
    auto delay_len_from_norm = [this](float n) -> size_t
    {
        if(n < 0.0f)
            n = 0.0f;
        else if(n > 1.0f)
            n = 1.0f;
        const float t_ms      = n * kDelayTimeMaxMs;
        const float t_samps_f = t_ms * sample_rate_ * (1.0f / 1000.0f);
        size_t      len       = (size_t)(t_samps_f + 0.5f);
        if(len < 1)
            len = 1;
        if(len >= kDelayMaxSamples)
            len = kDelayMaxSamples - 1;
        return len;
    };
    const size_t len_l = delay_len_from_norm(p.delay_time_l);
    const size_t len_r = delay_len_from_norm(p.delay_time_r);

    float dfb = p.delay_feedback;
    if(dfb < 0.0f)
        dfb = 0.0f;
    else if(dfb > 1.0f)
        dfb = 1.0f;
    static constexpr float kDelayFeedbackMax = 0.97f;
    const float            delay_fb          = dfb * kDelayFeedbackMax;

    float delay_wet_peak  = 0.0f;
    float reverb_wet_peak = 0.0f;

    // Load dry input into outL/outR once. If the caller passed the same buffer
    // for input and output (in-place, the common case for AudioCallback), the
    // memcpy is skipped. Per-stage processors below then operate on outL/outR
    // in place, preserving the original per-sample semantics of feeding the
    // output of each stage into the next.
    if(outL != inL)
        std::memcpy(outL, inL, size * sizeof(float));
    if(outR != inR)
        std::memcpy(outR, inR, size * sizeof(float));

    for(uint8_t stage_idx = 0; stage_idx < 4; ++stage_idx)
    {
        const uint8_t fx = p.fx_order[stage_idx];
        switch(fx)
        {
            case 0:
                if(sat_run)
                    ProcessSatBlock_(outL, outR, size, pre);
                break;
            case 1:
                if(eq_run)
                    ProcessEqBlock_(outL, outR, size, p.eq_mix);
                break;
            case 2:
                if(!sd_wav_load_busy && (delay_active_ || delay_tailing_))
                    ProcessDelayBlock_(outL, outR, size, p, len_l, len_r, delay_fb, delay_wet_peak);
                break;
            case 3:
                if(!sd_wav_load_busy && (reverb_active_ || reverb_tailing_))
                    ProcessReverbBlock_(outL, outR, size, p, reverb_wet_peak);
                break;
            default:
                break;
        }
    }

    if(diagnostics_)
    {
        float peak = 0.0f;
        for(size_t i = 0; i < size; ++i)
        {
            const float abs_l = std::fabs(outL[i]);
            const float abs_r = std::fabs(outR[i]);
            if(abs_l > peak)
                peak = abs_l;
            if(abs_r > peak)
                peak = abs_r;
        }
        DiagnosticsAccumulatePeakAtomic(
            diagnostics_->gain_probe_peak_bits[kDiagGainProbeFxPreMaster], peak);
    }

    // Final gain stage (and soft-clip safety when BOOST is engaged).
    ApplyMasterBlock_(outL, outR, size, level, bypass_comp);

    // During SDRAM WAV load, delay/reverb stages are skipped; freeze tail bookkeeping too
    // (otherwise wet_peak stays 0 and tails collapse incorrectly).
    if(!sd_wav_load_busy)
    {
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
}
