#include "voice_engine_render_internal.h"

void VoiceEngine::RenderBlockMixLayers_(float* outL, float* outR, size_t size, float mix_scale, uint32_t& clip_block)
{
    for(size_t i = 0; i < size; i++)
    {
        const float layer_a = ProcessLayerBusSample_(0u, outL[i]);
        const float layer_b = ProcessLayerBusSample_(1u, outR[i]);
        float mix = (layer_a + layer_b) * mix_scale;
        outL[i] = mix;
        outR[i] = mix;
        if(mix > 1.0f || mix < -1.0f)
            clip_block++;
    }
}
