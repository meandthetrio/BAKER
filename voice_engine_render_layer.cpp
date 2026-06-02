#include "voice_engine_render_internal.h"

#include "app_state_diagnostics.h"

#include <cmath>

void VoiceEngine::RenderBlockMixLayers_(float* outL,
                                        float* outR,
                                        size_t size,
                                        float mix_scale,
                                        uint32_t& clip_block,
                                        const bool (&layer_skip)[2])
{
    const bool diag_on = diagnostics_ != nullptr;

    // Both layers idle and decayed: bus was already memset to zero in
    // RenderBlock and no voice wrote to it, so the final mix is silence.
    if(layer_skip[0] && layer_skip[1])
        return;

    if(layer_skip[0])
    {
        // Layer A skipped: outL is silent, only process layer B.
        if(diag_on)
        {
            float peak_b_pre      = 0.0f;
            float peak_b_post     = 0.0f;
            float peak_sum_pre_fx = 0.0f;
            for(size_t i = 0; i < size; i++)
            {
                const float pre_b = std::fabs(outR[i]);
                if(pre_b > peak_b_pre)
                    peak_b_pre = pre_b;

                const float layer_b = ProcessLayerBusSample_(1u, outR[i]) * engine_layer_scale_[1u];
                const float post_b  = std::fabs(layer_b);
                if(post_b > peak_b_post)
                    peak_b_post = post_b;

                const float mix = layer_b * mix_scale;
                const float sum = std::fabs(mix);
                if(sum > peak_sum_pre_fx)
                    peak_sum_pre_fx = sum;

                outL[i] = mix;
                outR[i] = mix;
                if(mix > 1.0f || mix < -1.0f)
                    clip_block++;
            }

            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeBPre], peak_b_pre);
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeBPost], peak_b_post);
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeSumPreFx], peak_sum_pre_fx);
        }
        else
        {
            for(size_t i = 0; i < size; i++)
            {
                const float layer_b = ProcessLayerBusSample_(1u, outR[i]) * engine_layer_scale_[1u];
                const float mix     = layer_b * mix_scale;
                outL[i]             = mix;
                outR[i]             = mix;
                if(mix > 1.0f || mix < -1.0f)
                    clip_block++;
            }
        }
        return;
    }

    if(layer_skip[1])
    {
        // Layer B skipped: outR is silent, only process layer A.
        if(diag_on)
        {
            float peak_a_pre      = 0.0f;
            float peak_a_post     = 0.0f;
            float peak_sum_pre_fx = 0.0f;
            for(size_t i = 0; i < size; i++)
            {
                const float pre_a = std::fabs(outL[i]);
                if(pre_a > peak_a_pre)
                    peak_a_pre = pre_a;

                const float layer_a = ProcessLayerBusSample_(0u, outL[i]) * engine_layer_scale_[0u];
                const float post_a  = std::fabs(layer_a);
                if(post_a > peak_a_post)
                    peak_a_post = post_a;

                const float mix = layer_a * mix_scale;
                const float sum = std::fabs(mix);
                if(sum > peak_sum_pre_fx)
                    peak_sum_pre_fx = sum;

                outL[i] = mix;
                outR[i] = mix;
                if(mix > 1.0f || mix < -1.0f)
                    clip_block++;
            }

            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeAPre], peak_a_pre);
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeAPost], peak_a_post);
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeSumPreFx], peak_sum_pre_fx);
        }
        else
        {
            for(size_t i = 0; i < size; i++)
            {
                const float layer_a = ProcessLayerBusSample_(0u, outL[i]) * engine_layer_scale_[0u];
                const float mix     = layer_a * mix_scale;
                outL[i]             = mix;
                outR[i]             = mix;
                if(mix > 1.0f || mix < -1.0f)
                    clip_block++;
            }
        }
        return;
    }

    // Both layers active.
    if(diag_on)
    {
        float peak_a_pre      = 0.0f;
        float peak_b_pre      = 0.0f;
        float peak_a_post     = 0.0f;
        float peak_b_post     = 0.0f;
        float peak_sum_pre_fx = 0.0f;
        for(size_t i = 0; i < size; i++)
        {
            const float pre_a = std::fabs(outL[i]);
            const float pre_b = std::fabs(outR[i]);
            if(pre_a > peak_a_pre)
                peak_a_pre = pre_a;
            if(pre_b > peak_b_pre)
                peak_b_pre = pre_b;

            const float layer_a = ProcessLayerBusSample_(0u, outL[i]) * engine_layer_scale_[0u];
            const float layer_b = ProcessLayerBusSample_(1u, outR[i]) * engine_layer_scale_[1u];
            const float post_a  = std::fabs(layer_a);
            const float post_b  = std::fabs(layer_b);
            if(post_a > peak_a_post)
                peak_a_post = post_a;
            if(post_b > peak_b_post)
                peak_b_post = post_b;

            float       mix     = (layer_a + layer_b) * mix_scale;
            const float sum     = std::fabs(mix);
            if(sum > peak_sum_pre_fx)
                peak_sum_pre_fx = sum;
            outL[i]             = mix;
            outR[i]             = mix;
            if(mix > 1.0f || mix < -1.0f)
                clip_block++;
        }

        DiagnosticsAccumulatePeakAtomic(
            diagnostics_->gain_probe_peak_bits[kDiagGainProbeAPre], peak_a_pre);
        DiagnosticsAccumulatePeakAtomic(
            diagnostics_->gain_probe_peak_bits[kDiagGainProbeBPre], peak_b_pre);
        DiagnosticsAccumulatePeakAtomic(
            diagnostics_->gain_probe_peak_bits[kDiagGainProbeAPost], peak_a_post);
        DiagnosticsAccumulatePeakAtomic(
            diagnostics_->gain_probe_peak_bits[kDiagGainProbeBPost], peak_b_post);
        DiagnosticsAccumulatePeakAtomic(
            diagnostics_->gain_probe_peak_bits[kDiagGainProbeSumPreFx], peak_sum_pre_fx);
    }
    else
    {
        for(size_t i = 0; i < size; i++)
        {
            const float layer_a = ProcessLayerBusSample_(0u, outL[i]) * engine_layer_scale_[0u];
            const float layer_b = ProcessLayerBusSample_(1u, outR[i]) * engine_layer_scale_[1u];
            float       mix     = (layer_a + layer_b) * mix_scale;
            outL[i]             = mix;
            outR[i]             = mix;
            if(mix > 1.0f || mix < -1.0f)
                clip_block++;
        }
    }
}
