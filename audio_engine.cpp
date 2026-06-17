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
                                   const PerformParamsCurrent& p,
                                   float target_wet,
                                   float target_pre, float target_makeup,
                                   const float* send, float send_scale)
{
    const bool tape = (p.sat_mode == 0);

    // TAPE mode: push the (already 5 ms params-smoothed) controls into the L/R
    // tape saturators once per block. PrepareBlock smooths them and caches every
    // derived coefficient so the per-sample Process is just the DSP. The
    // saturator is full-wet; the bypass dry/wet blend below (sat_wet_gain_)
    // handles on/off so the stage tails cleanly.
    if(tape)
    {
        tape_sat_l_.PrepareBlock(p.sat_drive, p.sat_bump, p.sat_tone, p.sat_bias);
        tape_sat_r_.PrepareBlock(p.sat_drive, p.sat_bump, p.sat_tone, p.sat_bias);
    }

    // Drive-proportional makeup applied to the tape wet (see header). Linear in
    // drive on a dB scale: 0 dB at drive 0 -> kSatTapeMakeupDbAtFull at drive 1.
    const float target_tape_makeup
        = tape ? std::pow(10.0f, (kSatTapeMakeupDbAtFull * p.sat_drive) / 20.0f) : 1.0f;

    uint32_t hit_count = 0u;
    float    pre       = sat_pre_smoothed_;
    float    wet       = sat_wet_gain_;
    float    makeup    = sat_makeup_smoothed_;
    float    tape_mk   = sat_tape_makeup_smoothed_;
    for(size_t i = 0; i < n; ++i)
    {
        tape_mk += (target_tape_makeup - tape_mk) * kDelayFxSmoothCoeff;
        // Slow per-sample ramps (~50 ms). Removes drive-fader stepping and
        // smooths the bypass<->wet transition into a dry/wet blend. pre/makeup
        // are kept smoothed in both modes because the velmod send below always
        // runs through the lightweight SoftClip path.
        pre    += (target_pre    - pre)    * kDelayFxSmoothCoeff;
        wet    += (target_wet    - wet)    * kDelayFxSmoothCoeff;
        makeup += (target_makeup - makeup) * kDelayFxSmoothCoeff;
        const float dry_l = L[i];
        const float dry_r = R[i];
        float       wet_l;
        float       wet_r;
        if(tape)
        {
            wet_l = tape_sat_l_.Process(dry_l) * tape_mk;
            wet_r = tape_sat_r_.Process(dry_r) * tape_mk;
        }
        else
        {
            const float pre_l = dry_l * pre;
            const float pre_r = dry_r * pre;
            if(std::fabs(pre_l) > 1.0f)
                ++hit_count;
            if(std::fabs(pre_r) > 1.0f)
                ++hit_count;
            wet_l = SoftClip(pre_l) * makeup;
            wet_r = SoftClip(pre_r) * makeup;
        }
        float out_l = dry_l * (1.0f - wet) + wet_l * wet;
        float out_r = dry_r * (1.0f - wet) + wet_r * wet;
        if(send != nullptr)
        {
            // Velmod sat send: saturate the send and add its wet at unity —
            // independent of the global wet/dry mix (so a send-only activation,
            // where target_wet is 0, is still heard). Kept on the stateless
            // SoftClip path in both modes: routing it through the stateful tape
            // model would need a dedicated send instance and would lose this
            // fader-independence.
            const float s = send[i] * send_scale;
            out_l += SoftClip(s * pre) * makeup;
            out_r += SoftClip(s * pre) * makeup;
        }
        L[i] = out_l;
        R[i] = out_r;
    }
    sat_pre_smoothed_         = pre;
    sat_wet_gain_             = wet;
    sat_makeup_smoothed_      = makeup;
    sat_tape_makeup_smoothed_ = tape_mk;
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
                                     float& wet_peak,
                                     const float* send, float send_scale)
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
        // Velmod delay send: written into the line at FULL level so it echoes
        // regardless of the fader. SEND mode feeds the global dry scaled by mix
        // (the "send amount" INTO the line) so echoes can return at unity — that
        // makes the velmod send fully fader-independent, exactly like the reverb
        // send. MIX mode feeds full dry and crossfades at the output (insert).
        const float ssend = (send != nullptr) ? send[i] * send_scale : 0.0f;
        const float gscale = mix_mode ? fg : (fg * mix);
        const float inL = gscale * l + ssend;
        const float inR = gscale * r + ssend;

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
            // MIX mode (insert): crossfade dry/wet by the fader. A velmod send
            // here is still partly fader-dependent — use SEND mode for a fully
            // independent send.
            dl_out = l * (1.0f - mix) + dL * mix;
            dr_out = r * (1.0f - mix) + dR * mix;
        }
        else
        {
            // SEND mode: echoes return at UNITY (the fader already scaled the
            // input), so a velmod send fed at full is independent of the fader —
            // it rings even with the delay's own mix all the way down.
            dl_out = l + dL;
            dr_out = r + dR;
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
                                      float& wet_peak,
                                      const float* send, float send_scale,
                                      float wet_scale)
{
    const bool  feed = p.reverb_on;
    const float mix  = p.reverb_on ? p.reverb_mix : reverb_tail_mix_;
    const bool  mix_mode = (p.reverb_fader_mode == kReverbFaderModeMix);
    // Tail declick: wet_scale ramps 1->0 over the last blocks before the tail
    // backstop deactivates the stage, so the cap never chops an audible tail.
    // It only differs from 1.0 during that fade window.
    const float emix = mix * wet_scale;
    float       peak = wet_peak;

    // Fixed-size stack scratch (48-sample hardware block). in* is the explicit
    // tank input, tmp* the dattorro output.
    constexpr size_t kReverbScratchMax = 48;
    float tmpL[kReverbScratchMax], tmpR[kReverbScratchMax];
    float inL[kReverbScratchMax], inR[kReverbScratchMax];

    // Tank input = global feed + velmod send. Global feed: MIX mode sees the
    // full dry, SEND mode sees mix-scaled dry, the tail/off path feeds zero.
    // The velmod send is added at FULL level regardless of the fader, so its
    // reverb survives even with the reverb mix down (and even when reverb is
    // off — the FX-chain gate runs this stage when a reverb send is present).
    for(size_t i = 0; i < n; ++i)
    {
        const float gl = feed ? (mix_mode ? L[i] : L[i] * mix) : 0.0f;
        const float gr = feed ? (mix_mode ? R[i] : R[i] * mix) : 0.0f;
        const float s  = (send != nullptr) ? send[i] * send_scale : 0.0f;
        inL[i] = gl + s;
        inR[i] = gr + s;
    }
    dattorro_.ProcessBlock(inL, inR, tmpL, tmpR, n);

    for(size_t i = 0; i < n; ++i)
    {
        const float l = L[i];
        const float r = R[i];
        // wet = tank out - tank in removes ALL dry (global + send), leaving only
        // the reverb tail. DattorroReverb writes in + wet, so this holds for any
        // input scaling.
        const float wetL = tmpL[i] - inL[i];
        const float wetR = tmpR[i] - inR[i];

        float rl, rr;
        if(mix_mode)
        {
            // MIX mode crossfades dry/wet by the fader at the output, so a send
            // here is still partly fader-dependent (use SEND mode for a fully
            // independent send). emix folds in the tail declick: as it -> 0 the
            // dry comes back to full while the wet fades, so no level step.
            rl = l * (1.0f - emix) + wetL * emix;
            rr = r * (1.0f - emix) + wetR * emix;
        }
        else
        {
            // SEND mode (and all tail/off paths): dry + wet at unity. The send's
            // reverb comes through at unity, independent of the mix fader.
            // wet_scale applies the tail declick fade.
            rl = l + wetL * wet_scale;
            rr = r + wetR * wet_scale;
        }

        const float abs_rl = std::fabs(rl - l);
        const float abs_rr = std::fabs(rr - r);
        if(abs_rl > peak) peak = abs_rl;
        if(abs_rr > peak) peak = abs_rr;

        L[i] = rl;
        R[i] = rr;
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

    // TAPE-mode saturators (one per channel).
    tape_sat_l_.Init(sample_rate_, block_size_);
    tape_sat_r_.Init(sample_rate_, block_size_);

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
                               bool sd_wav_load_busy,
                               const float* const* send_bus,
                               bool sends_active)
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

    // Velmod 2b: a per-effect send keeps the effect "wanted on" so its tank/line
    // tail rings out after the sending voice ends (otherwise a send-only reverb
    // cuts the moment the note's release finishes). Sat is memoryless — no tail.
    const bool rev_send_present
        = sends_active && (send_bus != nullptr) && (send_bus[0] != nullptr);
    const bool dly_send_present
        = sends_active && (send_bus != nullptr) && (send_bus[1] != nullptr);

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
    if(p.delay_on || dly_send_present)
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
            delay_tail_heard_       = false; // wait for the first echo before
                                            // arming the quiet-stop detector
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
    if(p.reverb_on || rev_send_present)
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
    // Measured 2026-06-12: without makeup, sat_drive 0->1 added 0..+17 dB to
    // the FX-pre-master probe. `1/pre^0.83` fits the measured curve to ~1 dB.
    const float target_sat_makeup = 1.0f / std::pow(target_sat_pre, 0.83f);
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

    // Arm a brief INPUT ramp when fx_order changes — the signal path swaps
    // discontinuously, and if a stateful stage (delay/reverb) writes the
    // boundary sample directly into its buffer it captures the click and
    // feeds it back forever. Ramping the input before the FX chain runs
    // means every stage (including delay's write path) sees a smoothly
    // attenuated signal across the swap.
    bool fx_order_changed = false;
    for(uint8_t i = 0; i < 4; ++i)
    {
        if(fx_order_prev_[i] != p.fx_order[i])
        {
            fx_order_changed = true;
            fx_order_prev_[i] = p.fx_order[i];
        }
    }
    if(fx_order_changed)
        fx_order_declick_remaining_ = kFxOrderDeclickSamples;

    if(fx_order_declick_remaining_ > 0u)
    {
        for(size_t i = 0; i < size && fx_order_declick_remaining_ > 0u; ++i)
        {
            const float phase = 1.0f - (static_cast<float>(fx_order_declick_remaining_)
                                        / static_cast<float>(kFxOrderDeclickSamples));
            const float g = 0.5f - 0.5f * std::cos(3.14159265f * phase);
            outL[i] *= g;
            outR[i] *= g;
            --fx_order_declick_remaining_;
        }
    }

    // Velmod 2b (true wet-send): each per-effect send is fed into the effect's
    // wet generator at full level (inside the effect), bypassing the global
    // dry/wet fader — so per-voice sends are heard even with the effect's fader
    // down. Send index: 0=reverb, 1=delay, 2=sat. nullptr = that effect got no
    // send. The effect runs if it's globally active OR a send routed to it.
    // kSendFeedScale is the send-into-effect level (tunable by ear).
    constexpr float kSendFeedScale = 0.7f;
    const float* rev_send = (send_bus != nullptr) ? send_bus[0] : nullptr;
    const float* dly_send = (send_bus != nullptr) ? send_bus[1] : nullptr;
    const float* sat_send = (send_bus != nullptr) ? send_bus[2] : nullptr;
    if(!sends_active)
    {
        rev_send = nullptr;
        dly_send = nullptr;
        sat_send = nullptr;
    }

    // Reverb tail declick: ramp the wet to silence over the last
    // kReverbTailFadeBlocks before the backstop cap deactivates the stage, so a
    // long tail (raised max decay) is never chopped. reverb_tail_blocks_left_ is
    // decremented later in the tail bookkeeping, so it holds this block's value
    // here. 1.0 except during the fade window (or when not tailing).
    float reverb_wet_scale = 1.0f;
    if(reverb_tailing_ && reverb_tail_blocks_left_ <= kReverbTailFadeBlocks)
    {
        reverb_wet_scale = static_cast<float>(reverb_tail_blocks_left_)
                         / static_cast<float>(kReverbTailFadeBlocks);
    }

    for(uint8_t stage_idx = 0; stage_idx < 4; ++stage_idx)
    {
        const uint8_t fx = p.fx_order[stage_idx];
        switch(fx)
        {
            case 0:
                if(sat_run || sat_send != nullptr)
                {
                    const uint32_t stage_start = DWT->CYCCNT;
                    ProcessSatBlock_(outL, outR, size, p, target_sat_wet,
                                     target_sat_pre, target_sat_makeup, sat_send,
                                     kSendFeedScale);
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
                if(!sd_wav_load_busy && (delay_active_ || delay_tailing_ || dly_send != nullptr))
                {
                    const uint32_t stage_start = DWT->CYCCNT;
                    ProcessDelayBlock_(outL, outR, size, p, len_l, len_r, delay_fb,
                                       delay_wet_peak, dly_send, kSendFeedScale);
                    delay_cycles += DWT->CYCCNT - stage_start;
                }
                break;
            case 3:
                if(!sd_wav_load_busy && (reverb_active_ || reverb_tailing_ || rev_send != nullptr))
                {
                    const uint32_t stage_start = DWT->CYCCNT;
                    ProcessReverbBlock_(outL, outR, size, p, reverb_wet_peak,
                                        rev_send, kSendFeedScale, reverb_wet_scale);
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
            if(delay_wet_peak >= kTailSilenceThresh)
            {
                delay_tail_heard_   = true; // an echo returned
                delay_quiet_blocks_ = 0;
            }
            else if(delay_tail_heard_)
            {
                // Only count quiet once we've actually heard an echo — otherwise
                // the pre-echo gap (up to the delay time) would trip the
                // quiet-stop and the buffer clear would wipe the in-flight send.
                delay_quiet_blocks_++;
            }

            if(delay_tail_blocks_left_ > 0) delay_tail_blocks_left_--;

            if(delay_tail_blocks_left_ == 0
               || (delay_tail_heard_ && delay_quiet_blocks_ >= kDelayQuietBlocksToStop))
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
        else if(!p.delay_on && !dly_send_present)
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
        else if(!p.reverb_on && !rev_send_present)
        {
            // Keep reverb_active_ asserted while a velmod send is still feeding
            // the tank — otherwise this would clear it every block and the
            // send->silence falling edge would never trigger the tail (the
            // reverb would cut the instant the note ends).
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
