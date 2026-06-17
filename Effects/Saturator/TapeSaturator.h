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
        low_bump_lp_.Init(sample_rate, 90.0f);
        high_emph_lp_.Init(sample_rate, 2000.0f);
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

        const float low  = low_bump_lp_.Process(x);
        const float high = x - high_emph_lp_.Process(x);
        const float mid  = x - low - high;
        const float mid_res_sample = (mid + (prev_y_ * 0.35f)) * c_mid_res_;
        x = x + (low * c_low_) + (mid * c_bump_mid_) + mid_res_sample + (high * c_high_);

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
        const float xb = x * (1.0f + drive_term) + c_bias_;
        float       y  = FastTanh(xb) + c_mem_ * prev_y_;
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
        float cutoff = 16000.0f - (drive * 8000.0f);
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
        const float pre_emph_boost = 1.0f + drive_;
        const float low_amt        = (0.06f + 0.08f * (1.0f - tone_)) * pre_emph_boost;
        const float high_amt       = (0.02f + 0.08f * tone_) * pre_emph_boost;
        const float bump_curve     = bump_ * bump_;
        c_low_      = low_amt + (bump_curve * 0.25f); // coefficient on `low`
        c_high_     = high_amt;
        c_bump_mid_ = bump_curve * 1.6f;
        c_mid_res_  = bump_curve * 1.8f;
        c_comp_     = 0.35f * drive_;
        const float drive_boost = (1.0f + drive_) * (1.0f + drive_);
        c_drive_num_ = drive_ * 2.5f * drive_boost;
        c_bias_      = bias_ * 0.12f;
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
    float c_low_       = 0.0f;
    float c_high_      = 0.0f;
    float c_bump_mid_  = 0.0f;
    float c_mid_res_   = 0.0f;
    float c_comp_      = 0.0f;
    float c_drive_num_ = 0.0f;
    float c_bias_      = 0.0f;
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
    OnePoleLp low_bump_lp_;
    OnePoleLp high_emph_lp_;
    OnePoleLp post_lp_;
};
