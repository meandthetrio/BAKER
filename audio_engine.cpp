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
    delay_clear_pending_ = false;
    delay_clear_cursor_  = 0;
    delay_activate_pending_ = false;
    delay_feed_gain_     = 0.0f;
    delay_wet_mix_       = 0.0f;
    delay_len_l_smoothed_ = 0.0f;
    delay_len_r_smoothed_ = 0.0f;
}

// Step one chunk of the amortized delay-buffer clear. Called every ProcessBlock
// while `delay_clear_pending_` is true. The chunk size is bounded so the
// SDRAM memset fits comfortably inside the audio block budget.
void AudioEngine::DelayClearStep_()
{
    if(!delay_clear_pending_)
        return;
    size_t remaining = kDelayMaxSamples - delay_clear_cursor_;
    size_t chunk     = remaining < kDelayClearChunk ? remaining : kDelayClearChunk;
    std::memset(g_delay_buf_L + delay_clear_cursor_, 0, chunk * sizeof(float));
    std::memset(g_delay_buf_R + delay_clear_cursor_, 0, chunk * sizeof(float));
    delay_clear_cursor_ += chunk;
    if(delay_clear_cursor_ >= kDelayMaxSamples)
    {
        delay_clear_pending_ = false;
        delay_clear_cursor_  = 0;
    }
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

void AudioEngine::ProcessSatBlock_(float* L, float* R, size_t n,
                                   float target_pre, float target_wet)
{
    uint32_t hit_count = 0u;
    float    pre       = sat_pre_smoothed_;
    float    wet       = sat_wet_gain_;
    for(size_t i = 0; i < n; ++i)
    {
        // Slow per-sample ramps (~50 ms). Removes drive-fader stepping and
        // smooths the bypass<->softclip transition into a wet/dry blend.
        pre += (target_pre - pre) * kDelayFxSmoothCoeff;
        wet += (target_wet - wet) * kDelayFxSmoothCoeff;
        const float dry_l = L[i];
        const float dry_r = R[i];
        const float pre_l = dry_l * pre;
        const float pre_r = dry_r * pre;
        if(std::fabs(pre_l) > 1.0f)
            ++hit_count;
        if(std::fabs(pre_r) > 1.0f)
            ++hit_count;
        const float clip_l = SoftClip(pre_l);
        const float clip_r = SoftClip(pre_r);
        L[i] = dry_l * (1.0f - wet) + clip_l * wet;
        R[i] = dry_r * (1.0f - wet) + clip_r * wet;
    }
    sat_pre_smoothed_ = pre;
    sat_wet_gain_     = wet;
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
    const float target_feed = p.delay_on ? 1.0f : 0.0f;
    // Both branches below feed the same internal one-pole. The tail branch
    // (latched delay_tail_mix_) used to take effect instantly on the active->
    // tail flip and revert instantly on the tail->active flip, producing a
    // step of dL * (latched - current) that played back as a click. The
    // internal smoother makes the rejoin continuous.
    const float target_mix  = p.delay_on ? p.delay_mix : delay_tail_mix_;
    const float target_len_l_f = static_cast<float>(len_l);
    const float target_len_r_f = static_cast<float>(len_r);
    const bool  mix_mode    = (p.delay_fader_mode == kDelayFaderModeMix);
    float       peak        = wet_peak;

    // Hoist write index and smoother state into locals for the duration of
    // the block so the compiler keeps them in registers; write back once.
    size_t wr     = delay_wr_;
    float  fg     = delay_feed_gain_;
    float  mix    = delay_wet_mix_;
    float  len_lf = delay_len_l_smoothed_;
    float  len_rf = delay_len_r_smoothed_;
    for(size_t i = 0; i < n; ++i)
    {
        // Slow one-pole ramps (~50 ms). Removes every fader-driven
        // discontinuity: the input gate at activate/deactivate, the
        // latched->current mix step at the tail boundary, encoder steps on
        // LTM/RTM, and any rapid user fader motion.
        fg     += (target_feed    - fg)     * kDelayFxSmoothCoeff;
        mix    += (target_mix     - mix)    * kDelayFxSmoothCoeff;
        len_lf += (target_len_l_f - len_lf) * kDelayFxSmoothCoeff;
        len_rf += (target_len_r_f - len_rf) * kDelayFxSmoothCoeff;
        const float l   = L[i];
        const float r   = R[i];
        const float inL = fg * l;
        const float inR = fg * r;

        // Fractional read taps with linear interpolation. The integer part of
        // len picks the nearer cell, the fractional part blends in the next-
        // older cell so the read position glides between integers.
        const size_t len_li = static_cast<size_t>(len_lf);
        const size_t len_ri = static_cast<size_t>(len_rf);
        const float  fracl  = len_lf - static_cast<float>(len_li);
        const float  fracr  = len_rf - static_cast<float>(len_ri);
        const size_t rdL0 = (wr + kDelayMaxSamples - len_li)         % kDelayMaxSamples;
        const size_t rdL1 = (rdL0 + kDelayMaxSamples - 1)            % kDelayMaxSamples;
        const size_t rdR0 = (wr + kDelayMaxSamples - len_ri)         % kDelayMaxSamples;
        const size_t rdR1 = (rdR0 + kDelayMaxSamples - 1)            % kDelayMaxSamples;
        const float dL = g_delay_buf_L[rdL0] * (1.0f - fracl)
                         + g_delay_buf_L[rdL1] * fracl;
        const float dR = g_delay_buf_R[rdR0] * (1.0f - fracr)
                         + g_delay_buf_R[rdR1] * fracr;

        g_delay_buf_L[wr] = inL + dL * fb;
        g_delay_buf_R[wr] = inR + dR * fb;
        wr                = (wr + 1) % kDelayMaxSamples;

        // Dry/wet math must not depend on `feed`: the old code switched between
        // additive (feed=false) and crossfade (feed=true) inside mix-mode,
        // producing a dry step of l*mix at the gate flip. Now mix-mode is
        // consistent across the tail boundary.
        float dl_out, dr_out;
        if(mix_mode)
        {
            dl_out = l * (1.0f - mix) + dL * mix;
            dr_out = r * (1.0f - mix) + dR * mix;
        }
        else
        {
            dl_out = l + dL * mix;
            dr_out = r + dR * mix;
        }

        const float abs_dl = std::fabs(dl_out - l);
        const float abs_dr = std::fabs(dr_out - r);
        if(abs_dl > peak) peak = abs_dl;
        if(abs_dr > peak) peak = abs_dr;

        L[i] = dl_out;
        R[i] = dr_out;
    }
    delay_wr_             = wr;
    delay_feed_gain_      = fg;
    delay_wet_mix_        = mix;
    delay_len_l_smoothed_ = len_lf;
    delay_len_r_smoothed_ = len_rf;
    wet_peak              = peak;
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

            // Match feed-on dry/wet math so mix-mode does not step across the
            // active->tail boundary.
            float rl, rr;
            if(mix_mode)
            {
                rl = l * (1.0f - mix) + wetL * mix;
                rr = r * (1.0f - mix) + wetR * mix;
            }
            else
            {
                rl = l + wetL * mix;
                rr = r + wetR * mix;
            }

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
    const uint32_t fx_total_start = DWT->CYCCNT;
    uint32_t       sat_cycles     = 0u;
    uint32_t       eq_cycles      = 0u;
    uint32_t       delay_cycles   = 0u;
    uint32_t       reverb_cycles  = 0u;
    uint32_t       master_cycles  = 0u;

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
    // Buffer-cleanliness invariant: between deactivation and next activation,
    // the buffer must be fully zeroed. Otherwise dL reads through a sharp
    // discontinuity between leftover live audio and feedback-decay residue,
    // and that step gets written back via dL*fb into the ring — it then
    // echoes indefinitely. The cursor only runs in the fully-off state (NOT
    // during tail; the tail still needs the buffer to decay naturally). If
    // the user re-activates while the cursor is in flight, activation is
    // parked and ProcessDelayBlock_ stays skipped — user hears dry only for
    // up to ~12 ms until the cursor finishes.
    if(p.delay_on)
    {
        if(delay_clear_pending_)
        {
            delay_activate_pending_ = true; // wait for cursor
        }
        else
        {
            delay_activate_pending_ = false;
            delay_active_  = true;
            delay_tailing_ = false;
            delay_quiet_blocks_ = 0;
        }
    }
    else
    {
        delay_activate_pending_ = false;
        if(delay_active_ && !delay_tailing_)
        {
            delay_tailing_ = true;
            delay_tail_blocks_left_ = kDelayTailMaxBlocks;
            delay_quiet_blocks_     = 0;
            delay_tail_mix_         = p.delay_mix;
            // Cursor is NOT started here — tail needs the buffer alive.
        }
    }

    // ---- Reverb ON/OFF -> active/tail ----
    // No in-callback ReverbClear_() — it called dattorro_.Init() which clears
    // every internal allpass/delay buffer synchronously and was the source of
    // the reverb toggle click. The tank decays naturally to below kTailSilence-
    // Thresh (1e-4 / -80 dBFS) during the quiet-block window, so re-activation
    // picks up from inaudible state. Boot Init() still does the full clear.
    if(p.reverb_on)
    {
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

    // Step the amortized delay clear (no-op when not pending).
    DelayClearStep_();

    // If the user requested activation while the cursor was in flight and the
    // cursor just finished, activate now. The next FX-chain dispatch in this
    // same block will see delay_active_ true and start ProcessDelayBlock_
    // against a fully clean buffer.
    if(delay_activate_pending_ && !delay_clear_pending_)
    {
        delay_activate_pending_ = false;
        delay_active_           = true;
        delay_tailing_          = false;
        delay_quiet_blocks_     = 0;
    }

    // ---- SAT ----
    // Targets are sampled from current (already params-smoothed at 5 ms);
    // ProcessSatBlock_ smooths them further across samples at ~50 ms and
    // crossfades into the dry path via a wet gain. The stage keeps running
    // until the wet gain has decayed near zero so the off transition tail
    // doesn't step.
    const bool  sat_on_target = (p.sat_on && p.sat_drive >= 0.0001f);
    const float target_sat_pre = 1.0f + p.sat_drive * 10.0f;
    const float target_sat_wet = sat_on_target ? 1.0f : 0.0f;
    constexpr float kSatWetEpsilon = 1e-4f;
    const bool  sat_run = sat_on_target || (sat_wet_gain_ > kSatWetEpsilon);

    if(!sd_wav_load_busy)
        ReverbUpdateParamsDattorro_(p);

    const bool eq_run = true;
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
                {
                    const uint32_t stage_start = DWT->CYCCNT;
                    ProcessSatBlock_(outL, outR, size, target_sat_pre, target_sat_wet);
                    sat_cycles += DWT->CYCCNT - stage_start;
                }
                break;
            case 1:
                if(eq_run)
                {
                    const uint32_t stage_start = DWT->CYCCNT;
                    ProcessEqBlock_(outL, outR, size, 1.0f);
                    eq_cycles += DWT->CYCCNT - stage_start;
                }
                break;
            case 2:
                if(!sd_wav_load_busy && (delay_active_ || delay_tailing_))
                {
                    const uint32_t stage_start = DWT->CYCCNT;
                    ProcessDelayBlock_(outL, outR, size, p, len_l, len_r, delay_fb, delay_wet_peak);
                    delay_cycles += DWT->CYCCNT - stage_start;
                }
                break;
            case 3:
                if(!sd_wav_load_busy && (reverb_active_ || reverb_tailing_))
                {
                    const uint32_t stage_start = DWT->CYCCNT;
                    ProcessReverbBlock_(outL, outR, size, p, reverb_wet_peak);
                    reverb_cycles += DWT->CYCCNT - stage_start;
                }
                break;
            default:
                break;
        }
    }

    // Pre-master gain probe. This per-sample meter is diagnostics-only, so its time
    // is measured and subtracted from the fx-total bucket below — otherwise opening
    // the overlay would inflate its own "fx total" reading.
    uint32_t fx_probe_cycles = 0u;
    if(diagnostics_)
    {
        const uint32_t probe_start = DWT->CYCCNT;
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
        fx_probe_cycles = DWT->CYCCNT - probe_start;
    }

    // Final gain stage (and soft-clip safety when BOOST is engaged).
    const uint32_t master_start = DWT->CYCCNT;
    ApplyMasterBlock_(outL, outR, size, level, bypass_comp);
    master_cycles = DWT->CYCCNT - master_start;

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
                // Tail just ended: buffer has the previously-live audio
                // (cells wr hasn't wrapped over) plus tail-decay residue in
                // wr's range. Kick off the amortized cursor here so the
                // sharp boundary between those regions is zeroed out before
                // the next activation can read through it.
                delay_clear_pending_ = true;
                delay_clear_cursor_  = 0;
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
                // No synchronous ReverbClear_(): tank has decayed below
                // kTailSilenceThresh and any residual is inaudible.
            }
        }
        else if(!p.reverb_on)
        {
            reverb_active_ = false;
        }
    }

    if(diagnostics_)
    {
        DiagnosticsStoreCycleBucket(*diagnostics_, kDiagAudioBucketSat, sat_cycles);
        DiagnosticsStoreCycleBucket(*diagnostics_, kDiagAudioBucketEq, eq_cycles);
        DiagnosticsStoreCycleBucket(*diagnostics_, kDiagAudioBucketDelay, delay_cycles);
        DiagnosticsStoreCycleBucket(*diagnostics_, kDiagAudioBucketReverb, reverb_cycles);
        DiagnosticsStoreCycleBucket(*diagnostics_, kDiagAudioBucketMaster, master_cycles);
        const uint32_t fx_total_cycles = DWT->CYCCNT - fx_total_start;
        DiagnosticsStoreCycleBucket(*diagnostics_,
                                    kDiagAudioBucketFxTotal,
                                    (fx_total_cycles > fx_probe_cycles)
                                        ? (fx_total_cycles - fx_probe_cycles)
                                        : 0u);
    }
}
