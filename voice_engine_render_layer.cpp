#include "voice_engine_render_internal.h"

#include "app_state_diagnostics.h"

#include <cmath>

void VoiceEngine::RenderBlockMixLayers_(float* outL,
                                        float* outR,
                                        size_t size,
                                        float mix_scale,
                                        uint32_t& clip_block,
                                        const bool (&layer_skip)[2],
                                        uint32_t& emphasis_cycles,
                                        uint32_t& sum_cycles)
{
    const bool diag_on = diagnostics_ != nullptr;
    emphasis_cycles = 0u;
    sum_cycles = 0u;

    // Both layers idle and decayed: bus was already memset to zero in
    // RenderBlock and no voice wrote to it, so the final mix is silence.
    if(layer_skip[0] && layer_skip[1])
        return;

    // Pre-emphasis gain probes (diag-only, intentionally OUTSIDE the timed regions
    // below so the emphasis/sum CPU buckets are not inflated by the meters while the
    // overlay is open).
    if(diag_on)
    {
        float peak_a_pre = 0.0f;
        float peak_b_pre = 0.0f;
        for(size_t i = 0; i < size; i++)
        {
            if(!layer_skip[0])
            {
                const float a = std::fabs(outL[i]);
                if(a > peak_a_pre)
                    peak_a_pre = a;
            }
            if(!layer_skip[1])
            {
                const float b = std::fabs(outR[i]);
                if(b > peak_b_pre)
                    peak_b_pre = b;
            }
        }
        if(!layer_skip[0])
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeAPre], peak_a_pre);
        if(!layer_skip[1])
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeBPre], peak_b_pre);
    }

    // Phase 1: per-layer emphasis ladder, in place (the expensive part). Skipped
    // layers keep their already-zero bus. Timed into the emphasis bucket.
    const uint32_t emph_start = diag_on ? DWT->CYCCNT : 0u;
    if(!layer_skip[0])
        ProcessLayerBusBlock_(0u, outL, size, engine_layer_scale_[0u]);
    if(!layer_skip[1])
        ProcessLayerBusBlock_(1u, outR, size, engine_layer_scale_[1u]);
    if(diag_on)
        emphasis_cycles = DWT->CYCCNT - emph_start;

    // Post-emphasis gain probes (diag-only, untimed).
    if(diag_on)
    {
        float peak_a_post = 0.0f;
        float peak_b_post = 0.0f;
        for(size_t i = 0; i < size; i++)
        {
            if(!layer_skip[0])
            {
                const float a = std::fabs(outL[i]);
                if(a > peak_a_post)
                    peak_a_post = a;
            }
            if(!layer_skip[1])
            {
                const float b = std::fabs(outR[i]);
                if(b > peak_b_post)
                    peak_b_post = b;
            }
        }
        if(!layer_skip[0])
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeAPost], peak_a_post);
        if(!layer_skip[1])
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeBPost], peak_b_post);
    }

    // Phase 2: stereo sum + clip count, in place. Timed into the sum bucket.
    const uint32_t sum_start = diag_on ? DWT->CYCCNT : 0u;
    for(size_t i = 0; i < size; i++)
    {
        const float mix = (outL[i] + outR[i]) * mix_scale;
        outL[i] = mix;
        outR[i] = mix;
        if(mix > 1.0f || mix < -1.0f)
            clip_block++;
    }
    if(diag_on)
        sum_cycles = DWT->CYCCNT - sum_start;

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
