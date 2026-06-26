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

// Stepped render-to-preview context. The DSP is chunked across worker ticks so
// the main loop keeps animating the LED2 breathe during a long render (e.g. the
// STFT effects). Source -> chain -> SdManageBuffer; played back dry afterwards.
struct PreviewRenderCtx
{
    bool           active     = false;
    bool           has_effect = false;
    uint32_t       pos        = 0;
    uint32_t       length     = 0;
    const int16_t* src        = nullptr;
    int16_t*       dst        = nullptr;
    uint32_t       sample_rate = 48000u;
    int16_t        root_key    = 60;
};
PreviewRenderCtx s_preview;

// Samples processed per worker tick. Keeps each step short enough that the main
// loop (UI + LED breathe) stays responsive; render spans multiple ticks.
constexpr uint32_t kPreviewStepFrames = 8192u;

// Publish the result and hand off to the audio thread for dry auto-play. When
// use_source is true (no active effect) we play the unprocessed source.
void FinalizePreview(AppUiState& ui, AppSharedState& shared, bool use_source)
{
    auto&   bake = shared.bake_preview;
    Sample& rs   = bake.render_sample;
    if(use_source)
    {
        rs = bake.sample; // raw source, played dry
    }
    else
    {
        rs.pcm          = s_preview.dst;
        rs.length       = s_preview.length;
        rs.sample_rate  = s_preview.sample_rate;
        rs.root_key     = s_preview.root_key;
        rs.loop_start   = 0u;
        rs.loop_end     = s_preview.length;
        rs.loop_enabled = false;
    }
    // Order: render_sample + play_render visible before start_req (release).
    bake.play_render.store(1, std::memory_order_release);
    bake.craft_chain_active.store(0, std::memory_order_release);
    bake.start_req.store(1, std::memory_order_release);   // auto-play
    // Preview now matches the current config: clear dirty (LED -> green) and
    // drop the breathe. Safe to write ui here: the worker step runs on the main
    // loop, serialized with UI event handling (no concurrent edit mid-render).
    ui.craft_preview_dirty = false;
    bake.craft_render_active.store(0, std::memory_order_release);
    s_preview.active = false;
}

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

bool BeginCraftRenderPreview(AppUiState& ui, AppSharedState& shared)
{
    const Sample& src = shared.bake_preview.sample;
    if(src.pcm == nullptr || src.length == 0u)
        return false;

    const float sr = static_cast<float>(src.sample_rate ? src.sample_rate : 48000u);
    const craft::CraftChainConfig cfg = BuildConfigFromUi(ui);
    s_render_chain.ApplyConfig(cfg, sr);

    s_preview.has_effect  = s_render_chain.HasActiveEffect();
    s_preview.src         = src.pcm;
    s_preview.sample_rate = src.sample_rate ? src.sample_rate : 48000u;
    s_preview.root_key    = src.root_key;
    s_preview.pos         = 0;
    uint32_t length       = src.length;
    if(length > kSdManageMaxFrames)
        length = kSdManageMaxFrames;
    s_preview.length = length;
    s_preview.dst    = SdManageBuffer();
    if(s_preview.has_effect && s_preview.dst == nullptr)
        return false;

    s_preview.active = true;
    shared.bake_preview.craft_render_active.store(1, std::memory_order_release);
    return true;
}

bool StepCraftRenderPreview(AppUiState& ui, AppSharedState& shared)
{
    if(!s_preview.active)
        return true;

    // No active effect: nothing to render — auto-play the raw source dry.
    if(!s_preview.has_effect)
    {
        FinalizePreview(ui, shared, /*use_source=*/true);
        return true;
    }

    constexpr uint32_t kBlock = 256u;
    float              fbuf[kBlock];
    const uint32_t     step_end
        = (s_preview.pos + kPreviewStepFrames < s_preview.length)
              ? (s_preview.pos + kPreviewStepFrames)
              : s_preview.length;

    for(uint32_t pos = s_preview.pos; pos < step_end; pos += kBlock)
    {
        const uint32_t n = (step_end - pos < kBlock) ? (step_end - pos) : kBlock;
        for(uint32_t i = 0; i < n; ++i)
            fbuf[i] = static_cast<float>(s_preview.src[pos + i]) * (1.0f / 32768.0f);
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
            s_preview.dst[pos + i] = static_cast<int16_t>(s);
        }
    }
    s_preview.pos = step_end;

    if(s_preview.pos >= s_preview.length)
    {
        FinalizePreview(ui, shared, /*use_source=*/false);
        return true;
    }
    return false;
}
