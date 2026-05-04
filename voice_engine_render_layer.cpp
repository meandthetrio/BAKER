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
    // Both layers idle and decayed: bus was already memset to zero in
    // RenderBlock and no voice wrote to it, so the final mix is silence.
    if(layer_skip[0] && layer_skip[1])
        return;

    if(layer_skip[0])
    {
        // Layer A skipped: outL is silent, only process layer B.
        for(size_t i = 0; i < size; i++)
        {
            const float pre_b = std::fabs(outR[i]);
            if(diagnostics_)
                DiagnosticsAccumulatePeakAtomic(
                    diagnostics_->gain_probe_peak_bits[kDiagGainProbeBPre], pre_b);
            const float layer_b = ProcessLayerBusSample_(1u, outR[i]) * engine_layer_scale_[1u];
            if(diagnostics_)
                DiagnosticsAccumulatePeakAtomic(
                    diagnostics_->gain_probe_peak_bits[kDiagGainProbeBPost], std::fabs(layer_b));
            const float mix     = layer_b * mix_scale;
            if(diagnostics_)
                DiagnosticsAccumulatePeakAtomic(
                    diagnostics_->gain_probe_peak_bits[kDiagGainProbeSumPreFx], std::fabs(mix));
            outL[i]             = mix;
            outR[i]             = mix;
            if(mix > 1.0f || mix < -1.0f)
                clip_block++;
        }
        return;
    }

    if(layer_skip[1])
    {
        // Layer B skipped: outR is silent, only process layer A.
        for(size_t i = 0; i < size; i++)
        {
            const float pre_a = std::fabs(outL[i]);
            if(diagnostics_)
                DiagnosticsAccumulatePeakAtomic(
                    diagnostics_->gain_probe_peak_bits[kDiagGainProbeAPre], pre_a);
            const float layer_a = ProcessLayerBusSample_(0u, outL[i]) * engine_layer_scale_[0u];
            if(diagnostics_)
                DiagnosticsAccumulatePeakAtomic(
                    diagnostics_->gain_probe_peak_bits[kDiagGainProbeAPost], std::fabs(layer_a));
            const float mix     = layer_a * mix_scale;
            if(diagnostics_)
                DiagnosticsAccumulatePeakAtomic(
                    diagnostics_->gain_probe_peak_bits[kDiagGainProbeSumPreFx], std::fabs(mix));
            outL[i]             = mix;
            outR[i]             = mix;
            if(mix > 1.0f || mix < -1.0f)
                clip_block++;
        }
        return;
    }

    // Both layers active.
    for(size_t i = 0; i < size; i++)
    {
        const float pre_a = std::fabs(outL[i]);
        const float pre_b = std::fabs(outR[i]);
        if(diagnostics_)
        {
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeAPre], pre_a);
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeBPre], pre_b);
        }
        const float layer_a = ProcessLayerBusSample_(0u, outL[i]) * engine_layer_scale_[0u];
        const float layer_b = ProcessLayerBusSample_(1u, outR[i]) * engine_layer_scale_[1u];
        if(diagnostics_)
        {
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeAPost], std::fabs(layer_a));
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeBPost], std::fabs(layer_b));
        }
        float       mix     = (layer_a + layer_b) * mix_scale;
        if(diagnostics_)
            DiagnosticsAccumulatePeakAtomic(
                diagnostics_->gain_probe_peak_bits[kDiagGainProbeSumPreFx], std::fabs(mix));
        outL[i]             = mix;
        outR[i]             = mix;
        if(mix > 1.0f || mix < -1.0f)
            clip_block++;
    }
}
