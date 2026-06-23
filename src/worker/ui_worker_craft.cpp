#include "ui_worker_craft.h"

#include <cstdint>

#include "app_state_shared.h"
#include "app_state_ui.h"
#include "craft/craft_chain.h"
#include "sample_edit.h"
#include "sampler_sample.h"
#include "sd_sample_pool.h"
#include "ui_worker_sample_ops.h"

namespace {

// One CraftChain owned by the worker (independent of the audio thread's chain).
craft::CraftChain s_render_chain;

// Build a chain config from the live UI state (slot plugins + per-slot params).
craft::CraftChainConfig BuildConfigFromUi(const AppUiState& ui)
{
    craft::CraftChainConfig cfg{};
    for(uint8_t s = 0; s < craft::kCraftSlotCount; ++s)
    {
        const uint8_t plugin = static_cast<uint8_t>(ui.craft_slot_plugin[s] % craft::kCraftPluginCount);
        cfg.slots[s].plugin  = plugin;
        for(uint8_t p = 0; p < craft::kCraftMaxParams; ++p)
            cfg.slots[s].param[p] = ui.craft_param[s][plugin][p];
    }
    return cfg;
}

} // namespace

bool StartCraftRender(AppUiState& ui, AppSharedState& shared, bool overwrite)
{
    const Sample& src = shared.bake_preview.sample;
    if(src.pcm == nullptr || src.length == 0u)
        return false;

    const craft::CraftChainConfig cfg = BuildConfigFromUi(ui);
    s_render_chain.ApplyConfig(cfg, static_cast<float>(src.sample_rate ? src.sample_rate : 48000u));
    if(!s_render_chain.HasActiveEffect())
        return false; // nothing to print

    // Offline DSP pass: source int16 -> float block -> chain -> int16 dest.
    int16_t* dst = SdManageBuffer();
    if(dst == nullptr)
        return false;
    uint32_t length = src.length;
    if(length > kSdManageMaxFrames)
        length = kSdManageMaxFrames;

    constexpr uint32_t kBlock = 256u;
    float              fbuf[kBlock];
    for(uint32_t pos = 0; pos < length; pos += kBlock)
    {
        const uint32_t n = (length - pos < kBlock) ? (length - pos) : kBlock;
        for(uint32_t i = 0; i < n; ++i)
            fbuf[i] = static_cast<float>(src.pcm[pos + i]) * (1.0f / 32768.0f);
        s_render_chain.Process(fbuf, n);
        for(uint32_t i = 0; i < n; ++i)
        {
            float v = fbuf[i];
            if(v > 1.0f)
                v = 1.0f;
            else if(v < -1.0f)
                v = -1.0f;
            int32_t s = static_cast<int32_t>(v * 32767.0f);
            if(s > 32767)
                s = 32767;
            else if(s < -32768)
                s = -32768;
            dst[pos + i] = static_cast<int16_t>(s);
        }
    }

    // Publish the rendered buffer as the SD-manage sample so the existing save
    // pipeline can stream it to SD (same path used by trim save / replace).
    Sample& out      = shared.sd_manage.sample;
    out.pcm          = dst;
    out.length       = length;
    out.sample_rate  = 48000u;
    out.root_key     = src.root_key;
    out.loop_start   = 0u;
    out.loop_end     = length;
    out.loop_enabled = false;
    shared.sd_manage.edit = SampleEdit_Default(length);

    const char* replace_path = (overwrite && ui.craft_loaded_path[0] != '\0') ? ui.craft_loaded_path : nullptr;
    return StartSave(ui, shared, SampleSaveSource::SdManage, nullptr, replace_path);
}
