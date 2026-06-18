#include "voice_engine_render_internal.h"

#include "app_state_diagnostics.h"

#include <cmath>

// Cheap tanh approximation (was the emphasis ladder's saturator). Used as the
// layer-bus soft-clip so polyphonic peaks are limited smoothly instead of
// hard-clipping. Near-linear for |x| < ~0.5 (single notes pass clean) and
// compresses toward ±1 as more voices sum. Restores the incidental limiting the
// removed emphasis tanh used to provide on this bus.
static inline float LayerBusSoftClip_(float x)
{
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void VoiceEngine::RenderBlockMixLayers_(float* outL,
                                        float* outR,
                                        size_t size,
                                        float mix_scale,
                                        uint32_t& clip_block,
                                        const bool (&layer_skip)[2],
                                        uint32_t& sum_cycles)
{
    const bool diag_on = diagnostics_ != nullptr;
    sum_cycles = 0u;

    // Single layer: outL is the only voice bus (outR is the mono-dup output
    // channel). If the layer is idle the buffers were already memset to zero in
    // RenderBlock, so the output is silence — nothing to do.
    if(layer_skip[0])
        return;

    // Pre-scale gain probe (diag-only, intentionally OUTSIDE the timed region
    // below so the layer-mix CPU bucket is not inflated by the meter).
    if(diag_on)
    {
        float peak_a_pre = 0.0f;
        for(size_t i = 0; i < size; i++)
        {
            const float a = std::fabs(outL[i]);
            if(a > peak_a_pre)
                peak_a_pre = a;
        }
        DiagnosticsAccumulatePeakAtomic(
            diagnostics_->gain_probe_peak_bits[kDiagGainProbeAPre], peak_a_pre);
    }

    // Phase 1: layer output scale + soft-clip, in place. The soft-clip limits
    // polyphonic peaks (N voices summing past full-scale) the way the removed
    // emphasis tanh used to, so 5+ simultaneous notes no longer hard-clip.
    // Timed into the layer-mix bucket.
    const uint32_t scale_start = diag_on ? DWT->CYCCNT : 0u;
    {
        const float scale = engine_layer_scale_[0u];
        for(size_t i = 0; i < size; i++)
            outL[i] = LayerBusSoftClip_(outL[i] * scale);
    }
    const uint32_t scale_cycles = diag_on ? (DWT->CYCCNT - scale_start) : 0u;

    // Post-scale gain probe (diag-only, untimed).
    if(diag_on)
    {
        float peak_a_post = 0.0f;
        for(size_t i = 0; i < size; i++)
        {
            const float a = std::fabs(outL[i]);
            if(a > peak_a_post)
                peak_a_post = a;
        }
        DiagnosticsAccumulatePeakAtomic(
            diagnostics_->gain_probe_peak_bits[kDiagGainProbeAPost], peak_a_post);
    }

    // Phase 2: apply the mix scale and mono-dup into the stereo output + clip
    // count, in place. Timed into the sum bucket.
    const uint32_t sum_start = diag_on ? DWT->CYCCNT : 0u;
    for(size_t i = 0; i < size; i++)
    {
        const float mix = outL[i] * mix_scale;
        outL[i] = mix;
        outR[i] = mix;
        if(mix > 1.0f || mix < -1.0f)
            clip_block++;
    }
    if(diag_on)
        sum_cycles = scale_cycles + (DWT->CYCCNT - sum_start);

    // Sum-stage peak probe (diag-only, untimed).
    if(diag_on)
    {
        float peak_sum_pre_fx = 0.0f;
        for(size_t i = 0; i < size; i++)
        {
            const float s = std::fabs(outL[i]);
            if(s > peak_sum_pre_fx)
                peak_sum_pre_fx = s;
        }
        DiagnosticsAccumulatePeakAtomic(
            diagnostics_->gain_probe_peak_bits[kDiagGainProbeSumPreFx], peak_sum_pre_fx);
    }
}
