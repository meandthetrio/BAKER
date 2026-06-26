#include "ui_worker_craft.h"

#include <cstdint>

#include "sys/system.h"

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
// The DSP for a short sample finishes in a few ms — far too fast to see the
// 1 Hz breathe. So we PACE the render across a target window: the work is spread
// evenly over ~kRenderTargetMs (with small per-tick chunks so the main loop
// keeps redrawing the LED smoothly), and the breathe runs continuously DURING
// the render. A render whose real work exceeds the window simply runs flat out
// (still chunked, still breathing). No pre-lead, no post-hold.
constexpr uint32_t kRenderTargetMs = 1100u; // ~one breath cycle of visible render
constexpr uint32_t kRenderStepCap  = 4096u; // max frames per tick (loop stays responsive)

struct PreviewRenderCtx
{
    bool           active     = false;
    bool           has_effect = false;
    uint32_t       start_ms   = 0;       // indicator-on time (for the lead-in)
    uint32_t       pos        = 0;       // stream position 0..total
    uint32_t       length     = 0;       // real output frames
    uint32_t       latency    = 0;       // chain latency (leading samples to trim)
    uint32_t       total      = 0;       // length + latency (stream frames to feed)
    const int16_t* src        = nullptr;
    int16_t*       dst        = nullptr;
    uint32_t       sample_rate = 48000u;
    int16_t        root_key    = 60;
};
PreviewRenderCtx s_preview;

// Process one stream block [base, base+n), n <= 256, through s_render_chain with
// the STFT latency trim: input is source[k] while k < length else 0 (tail flush
// for the chain latency), output is written to dst[k - latency], discarding the
// leading `latency` pre-roll. Shared by the synchronous save render and the
// paced preview render so both trim identically. latency == 0 (non-STFT effects)
// degenerates to the plain 1:1 source->dst pass.
void RenderStreamBlock_(const int16_t* src,
                        uint32_t       length,
                        uint32_t       latency,
                        uint32_t       base,
                        uint32_t       n,
                        int16_t*       dst)
{
    float fbuf[256];
    for(uint32_t i = 0; i < n; ++i)
    {
        const uint32_t k = base + i;
        fbuf[i] = (k < length) ? (static_cast<float>(src[k]) * (1.0f / 32768.0f)) : 0.0f;
    }
    s_render_chain.Process(fbuf, n);
    for(uint32_t i = 0; i < n; ++i)
    {
        const uint32_t k = base + i;
        if(k < latency)
            continue; // pre-roll: discard
        const uint32_t di = k - latency;
        if(di >= length)
            continue;
        float v = fbuf[i];
        if(v > 1.0f) v = 1.0f;
        else if(v < -1.0f) v = -1.0f;
        int32_t s = static_cast<int32_t>(v * 32767.0f);
        if(s > 32767) s = 32767;
        else if(s < -32768) s = -32768;
        dst[di] = static_cast<int16_t>(s);
    }
}


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

    // Offline DSP pass: source int16 -> float block -> chain -> int16 dest. Feed
    // length + chain latency stream samples (source then zero flush) and trim the
    // leading latency so the saved file is time-aligned with the source (matches
    // the preview render; required for STFT effects like "fresh").
    int16_t* dst = SdManageBuffer();
    if(dst == nullptr)
        return false;
    uint32_t length = src.length;
    if(length > kSdManageMaxFrames)
        length = kSdManageMaxFrames;

    const uint32_t     latency = s_render_chain.Latency();
    const uint32_t     total   = length + latency;
    constexpr uint32_t kBlock  = 256u;
    for(uint32_t base = 0; base < total; base += kBlock)
    {
        const uint32_t n = (total - base < kBlock) ? (total - base) : kBlock;
        RenderStreamBlock_(src.pcm, length, latency, base, n, dst);
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
    s_preview.length  = length;
    s_preview.latency = s_render_chain.Latency();
    s_preview.total   = length + s_preview.latency;
    s_preview.dst     = SdManageBuffer();
    if(s_preview.has_effect && s_preview.dst == nullptr)
        return false;

    s_preview.start_ms = daisy::System::GetNow();
    s_preview.active   = true;
    shared.bake_preview.craft_render_active.store(1, std::memory_order_release);
    return true;
}

bool StepCraftRenderPreview(AppUiState& ui, AppSharedState& shared)
{
    if(!s_preview.active)
        return true;

    const uint32_t elapsed = daisy::System::GetNow() - s_preview.start_ms;

    // No active effect: nothing to render — breathe for the window, then play
    // the raw source dry.
    if(!s_preview.has_effect)
    {
        if(elapsed >= kRenderTargetMs)
        {
            FinalizePreview(ui, shared, /*use_source=*/true);
            return true;
        }
        return false;
    }

    // Pace: process only up to where the schedule has reached (work spread over
    // kRenderTargetMs), capped per tick so the loop keeps redrawing the LED. A
    // render heavier than the window falls behind the schedule and just runs at
    // the cap (flat out) — still chunked, still breathing.
    uint32_t sched = (elapsed >= kRenderTargetMs)
                         ? s_preview.total
                         : static_cast<uint32_t>(
                               static_cast<uint64_t>(s_preview.total) * elapsed / kRenderTargetMs);
    uint32_t step_end = (s_preview.pos + kRenderStepCap < sched) ? (s_preview.pos + kRenderStepCap) : sched;
    if(step_end < s_preview.pos)
        step_end = s_preview.pos; // schedule behind pos (clock skew): do nothing this tick

    constexpr uint32_t kBlock = 256u;
    for(uint32_t base = s_preview.pos; base < step_end; base += kBlock)
    {
        const uint32_t n = (step_end - base < kBlock) ? (step_end - base) : kBlock;
        RenderStreamBlock_(s_preview.src, s_preview.length, s_preview.latency, base, n, s_preview.dst);
    }
    s_preview.pos = step_end;

    // DSP finished -> finalize and auto-play immediately (no trailing hold).
    if(s_preview.pos >= s_preview.total)
    {
        FinalizePreview(ui, shared, /*use_source=*/false);
        return true;
    }
    return false;
}
