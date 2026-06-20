#pragma once

#include <cmath>
#include <cstddef>

// Multiband tape-saturation model. Ported from the meandthetrio/ADSR `Cuz`
// branch (audio_dsp.h). Self-contained: internal DC blockers, one-pole LPs,
// FastTanh waveshaper, envelope-following compression, bias/tone/bump controls,
// a memory/hysteresis term, drive-dependent post-LP gain.
//
// Mono per-sample processor — the audio engine owns one instance per channel
// (tape_sat_l_/tape_sat_r_), mirroring the `Cuz` sat_l/sat_r layout.
//
// CPU: the controls (drive/bump/tone/bias) change at most once per audio block,
// so everything derived from them is constant within a block. `PrepareBlock` is
// called once per block to smooth the params and cache all derived coefficients;
// `Process` is then a stripped per-sample inner loop that only touches genuine
// per-sample state (filters, env follower, the drive divide + tanh). This
// mirrors the per-block coefficient caching in voice_engine_emphasis.cpp and the
// control-rate hoist in DattorroReverb::ProcessBlock. Output is full-wet (the
// engine's sat_wet_gain_ handles the dry/wet bypass), so the old internal
// mix/output-gain blend is baked out.
class TapeSaturator
{
  public:
    void Init(float sample_rate, size_t block_size)
    {
        sample_rate_ = sample_rate;
        dc_pre_.Init(sample_rate, 20.0f);
        dc_post_.Init(sample_rate, 20.0f);
        bump_eq_.Init(sample_rate, 500.0f, 1.0f); // midrange bell, Q=1
        tone_tilt_lp_.Init(sample_rate, 800.0f);
        post_lp_.Init(sample_rate, 16000.0f);
        release_coeff_ = expf(-1.0f / (0.08f * sample_rate));
        // Per-block param smoothing (~20 ms). The params are already 5 ms
        // smoothed upstream; this is a light second stage so per-block coeff
        // steps stay click-free. Applied once per block, not per sample.
        constexpr float kSmoothTauSec = 0.02f;
        block_smooth_coeff_
            = 1.0f - expf(-static_cast<float>(block_size) / (kSmoothTauSec * sample_rate));
        drive_ = drive_target_ = 0.0f;
        bump_  = bump_target_  = 0.0f;
        tone_  = tone_target_  = 0.5f;
        bias_  = bias_target_  = 0.0f;
        env_    = 0.0f;
        prev_y_ = 0.0f;
        settled_ = false;
        DeriveCoeffs_();
    }

    // Once per block: smooth the controls and cache everything derived from
    // them. bias is bipolar (-1..1); drive/bump/tone are 0..1.
    //
    // Gate: when the smoothed controls have already converged to target (held
    // faders — the common case), skip the smoothing AND DeriveCoeffs_. That is
    // what skips the per-block expf (post-LP cutoff) + powf (post-gain). Mirrors
    // the EmphCoeffClose_ settle gate in voice_engine_emphasis.cpp.
    void PrepareBlock(float drive, float bump, float tone, float bias)
    {
        drive_target_ = Clamp(drive, 0.0f, 1.0f);
        bump_target_  = Clamp(bump, 0.0f, 1.0f);
        tone_target_  = Clamp(tone, 0.0f, 1.0f);
        bias_target_  = Clamp(bias, -1.0f, 1.0f);

        if(CoeffClose_(drive_target_, drive_) && CoeffClose_(bump_target_, bump_)
           && CoeffClose_(tone_target_, tone_) && CoeffClose_(bias_target_, bias_))
        {
            if(!settled_)
            {
                // One-time sub-epsilon snap + final derive so cached coeffs are
                // exact, then idle (no expf/powf) until a control moves again.
                drive_ = drive_target_;
                bump_  = bump_target_;
                tone_  = tone_target_;
                bias_  = bias_target_;
                DeriveCoeffs_();
                settled_ = true;
            }
            return;
        }

        settled_ = false;
        drive_ += block_smooth_coeff_ * (drive_target_ - drive_);
        bump_  += block_smooth_coeff_ * (bump_target_ - bump_);
        tone_  += block_smooth_coeff_ * (tone_target_ - tone_);
        bias_  += block_smooth_coeff_ * (bias_target_ - bias_);
        DeriveCoeffs_();
    }

    // Per-sample inner loop. Uses only cached coeffs (c_*) + per-sample state.
    float Process(float x)
    {
        x = dc_pre_.Process(x);

        // Tone: a real dB-symmetric tilt shelf around 800 Hz. tone<0.5 tilts
        // dark (lows up / highs down), tone>0.5 tilts bright. Center = flat
        // (c_tone_lo_==c_tone_hi_==1). Drive-independent, ~±8 dB at the extremes.
        const float tone_lp = tone_tilt_lp_.Process(x);
        const float tone_hp = x - tone_lp;
        x = tone_lp * c_tone_lo_ + tone_hp * c_tone_hi_;

        // Bump: a proper midrange bell (peaking EQ at 500 Hz, Q=1) up to +12 dB,
        // pushed into the saturator. Tighter and more focused than the old broad
        // one-pole bandpass; flat (unity passthrough) when bump is at 0.
        x = bump_eq_.Process(x);

        const float abs_x = fabsf(x);
        if(abs_x > env_)
        {
            env_ = abs_x;
        }
        else
        {
            env_ *= release_coeff_;
        }
        // eff_drive*2.5*drive_boost == c_drive_num_/(1+comp*env), with the
        // per-block numerator c_drive_num_ = drive*2.5*(1+drive)^2 hoisted out.
        const float drive_term = c_drive_num_ / (1.0f + c_comp_ * env_);
        const float driven     = x * (1.0f + drive_term);
        // Bias morph: `odd` is the pure (symmetric) tanh -> odd harmonics only.
        // odd*odd is an even-symmetric term -> pure even harmonics (its DC is
        // stripped by dc_post_). c_even_ crossfades from 0 at fader min (pure
        // odd) up to a strong even content at fader max, so the control sweeps
        // odd -> even across its whole travel.
        const float odd = FastTanh(driven);
        float       y   = odd + c_even_ * (odd * odd) + c_mem_ * prev_y_;
        prev_y_ = y;
        y = dc_post_.Process(y);
        y = post_lp_.Process(y);
        return y * c_post_gain_;
    }

  private:
    struct DcBlocker
    {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float r  = 0.0f;

        void Init(float sample_rate, float cutoff_hz)
        {
            r  = expf(-2.0f * 3.14159265f * cutoff_hz / sample_rate);
            x1 = 0.0f;
            y1 = 0.0f;
        }

        float Process(float x)
        {
            const float y = x - x1 + (r * y1);
            x1            = x;
            y1            = y;
            return y;
        }
    };

    struct OnePoleLp
    {
        float a = 0.0f;
        float y = 0.0f;

        void Init(float sample_rate, float cutoff_hz)
        {
            SetFreq(sample_rate, cutoff_hz);
            y = 0.0f;
        }

        void SetFreq(float sample_rate, float cutoff_hz)
        {
            a = expf(-2.0f * 3.14159265f * cutoff_hz / sample_rate);
        }

        float Process(float x)
        {
            y = (1.0f - a) * x + (a * y);
            return y;
        }
    };

    // RBJ peaking-EQ biquad (Direct Form I). Fixed centre frequency and Q; only
    // the gain moves at run time. cos(w0)/alpha are constant, so SetGainDb only
    // recomputes the gain-dependent coeffs (one powf), gated by PrepareBlock.
    struct PeakEq
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; // normalised
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
        float cos_w0_ = 1.0f, alpha_ = 0.0f;

        void Init(float sample_rate, float f0, float q)
        {
            const float w0 = 2.0f * 3.14159265f * f0 / sample_rate;
            cos_w0_ = cosf(w0);
            alpha_  = sinf(w0) / (2.0f * q);
            x1 = x2 = y1 = y2 = 0.0f;
            SetGainDb(0.0f);
        }

        void SetGainDb(float db)
        {
            const float A   = powf(10.0f, db / 40.0f);
            const float inv = 1.0f / (1.0f + alpha_ / A);
            b0 = (1.0f + alpha_ * A) * inv;
            b1 = (-2.0f * cos_w0_) * inv;
            b2 = (1.0f - alpha_ * A) * inv;
            a1 = (-2.0f * cos_w0_) * inv;
            a2 = (1.0f - alpha_ / A) * inv;
        }

        float Process(float x)
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };

    // Settle test: a control is "converged" when one smoothing step would move
    // it by less than a relative-with-floor epsilon. The (|t|+1) floor makes
    // near-zero values use ~absolute eps. Same form as EmphCoeffClose_.
    static bool CoeffClose_(float t, float c)
    {
        float d = t - c;
        if(d < 0.0f)
        {
            d = -d;
        }
        float a = t;
        if(a < 0.0f)
        {
            a = -a;
        }
        return d <= 1.0e-4f * (a + 1.0f);
    }

    static float Clamp(float v, float lo, float hi)
    {
        if(v < lo)
        {
            return lo;
        }
        if(v > hi)
        {
            return hi;
        }
        return v;
    }

    static float FastTanh(float x)
    {
        if(x > 3.0f)
        {
            x = 3.0f;
        }
        else if(x < -3.0f)
        {
            x = -3.0f;
        }
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    static float DbToGain(float db)
    {
        return powf(10.0f, db / 20.0f);
    }

    void UpdatePostCutoff(float drive)
    {
        // Start at 20 kHz (essentially open) so light saturation keeps its
        // highs, and only roll down as drive climbs. The drive*drive curve means
        // the first half of the fader barely touches the top end, reaching the
        // 8 kHz tape-style floor only near full drive.
        float cutoff = 20000.0f - (drive * drive * 12000.0f);
        if(cutoff < 8000.0f)
        {
            cutoff = 8000.0f;
        }
        post_lp_.SetFreq(sample_rate_, cutoff);
    }

    // Derive and cache every per-block-constant value from the smoothed
    // controls. Called once per block (PrepareBlock) and at Init.
    void DeriveCoeffs_()
    {
        // Tone = dB-symmetric tilt shelf (applied in Process around 800 Hz).
        // tilt -1 (dark) .. +1 (bright); exp() gives equal-and-opposite dB on the
        // two bands so center is exactly flat. kTiltDepth 0.92 ~= ±8 dB at full.
        constexpr float kTiltDepth = 0.92f;
        const float     tilt       = (tone_ - 0.5f) * 2.0f;
        c_tone_hi_ = expf(tilt * kTiltDepth);
        c_tone_lo_ = expf(-tilt * kTiltDepth);

        // Bump: 500 Hz peaking-EQ gain, 0..+12 dB on a bump^2 curve (gentle at the
        // bottom of the fader, full bell at the top).
        bump_eq_.SetGainDb(bump_ * bump_ * 12.0f);
        // Compression eased (0.18, was 0.35): the env follower backed the drive
        // off on loud signals and kept the sound polite. Less compression lets
        // the tanh stay driven, so harmonics bloom relative to the fundamental.
        c_comp_     = 0.18f * drive_;
        const float drive_boost = (1.0f + drive_) * (1.0f + drive_);
        // Drive gain up (4.0, was 2.5) for more harmonic generation per the
        // "hit harder" request.
        c_drive_num_ = drive_ * 4.0f * drive_boost;
        // Bias = odd->even morph (applied in Process as odd + c_even_*odd^2).
        // bias_ is stored bipolar (-1..1); (0.5 + 0.5*bias_) is the 0..1 fader
        // position. 0 at min = pure odd; kEvenDepth sets how even-rich max gets.
        constexpr float kEvenDepth = 0.7f;
        const float     bias_norm  = 0.5f + 0.5f * bias_;
        c_even_      = bias_norm * kEvenDepth;
        c_mem_       = 0.05f + 0.1f * drive_;
        UpdatePostCutoff(drive_);
        c_post_gain_ = DbToGain(-6.0f * drive_);
    }

    float sample_rate_        = 48000.0f;
    float block_smooth_coeff_ = 0.0f;
    float release_coeff_      = 0.0f;

    // Control targets + per-block-smoothed values.
    float drive_target_ = 0.0f;
    float bump_target_  = 0.0f;
    float tone_target_  = 0.5f;
    float bias_target_  = 0.0f;
    float drive_        = 0.0f;
    float bump_         = 0.0f;
    float tone_         = 0.5f;
    float bias_         = 0.0f;

    // Cached per-block coefficients (DeriveCoeffs_).
    float c_tone_lo_   = 1.0f;
    float c_tone_hi_   = 1.0f;
    float c_comp_      = 0.0f;
    float c_drive_num_ = 0.0f;
    float c_even_      = 0.0f;
    float c_mem_       = 0.0f;
    float c_post_gain_ = 1.0f;

    // True once the smoothed controls have converged to target — gates out the
    // per-block DeriveCoeffs_ (expf/powf) while the faders are held.
    bool settled_ = false;

    // Per-sample state.
    float env_    = 0.0f;
    float prev_y_ = 0.0f;

    DcBlocker dc_pre_;
    DcBlocker dc_post_;
    PeakEq    bump_eq_;
    OnePoleLp tone_tilt_lp_;
    OnePoleLp post_lp_;
};
