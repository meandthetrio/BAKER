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

// Stepped render-to-preview context. The DSP is chunked across worker ticks (one
// kRenderStepCap chunk per ~16 ms UI tick) so the main loop stays responsive during
// a long render (e.g. the STFT effects). Source -> chain -> SdManageBuffer; played
// back dry afterwards. The render runs FLAT OUT (no artificial pacing): the audition
// is muted while the shared buffer is rewritten, so we want it to finish as fast as
// the cap allows, not stretched to a fixed window. (An earlier model paced the work
// over a ~1.1 s "breath" window for a visible LED animation; that 1.1 s of muted
// dead-space per re-render was removed when knob edits began auto-re-rendering.)

// Per-tick render budget (frames rendered per worker tick). Tradeoff: a BIGGER cap
// finishes the whole-buffer render sooner but does more STFT work in one main-loop
// pass, so a single chunk can exceed the ~16 ms UI-tick interval and monopolize the
// loop — freezing the encoder and flickering the LED while a render runs.
//
// With Step 3 (wrapped, playhead-anchored render) the user HEARS a param change at
// ~kPreviewReRenderMargin (~150 ms) regardless of how long the full buffer takes to
// finish — only the inaudible background catch-up is slower. So we deliberately keep
// the cap SMALL: each chunk stays well under a UI tick (loop stays free for input),
// while throughput stays comfortably above 1x real-time so the render frontier keeps
// outrunning the playhead. Floor is ~768 frames/tick (= 1x @ 48k/16ms); below that
// the frontier can't stay ahead and Step 3 breaks. 1024 ≈ 1.3x real-time.
// kRenderStepCapMax is a HARD ceiling so any future tuning can never reintroduce a
// loop-stalling chunk — always clamp dynamic step sizes to it.
constexpr uint32_t kRenderStepCapMax = 8192u; // hard ceiling — do not exceed
constexpr uint32_t kRenderStepCap    = 1024u; // tunable: max frames per tick (~1.3x RT)
static_assert(kRenderStepCap <= kRenderStepCapMax,
              "kRenderStepCap must stay within the kRenderStepCapMax loop-stall ceiling");
static_assert(kRenderStepCap >= 768u,
              "kRenderStepCap must stay above ~1x real-time or the Step 3 frontier falls behind");

// Step 3: a re-render of an already-looping audition renders the buffer in WRAPPED
// order, anchored this many frames AHEAD of the live playhead, so the audio (which
// keeps looping, no mute) reaches the freshly-rendered region after ~this delay.
// ~150 ms @ 48 kHz. Must exceed the chain latency (the wrapped render's first write
// lands `latency` frames before this point). Tunable.
constexpr uint32_t kPreviewReRenderMargin = 7200u;

struct PreviewRenderCtx
{
    bool           active     = false;
    bool           has_effect = false;
    bool           wrapped    = false;   // true = playhead-anchored re-render (Step 3)
    uint32_t       start_ms   = 0;       // indicator-on time (for the lead-in)
    uint32_t       pos        = 0;       // feed cursor 0..total
    uint32_t       length     = 0;       // real output frames
    uint32_t       latency    = 0;       // chain latency (leading samples to trim)
    uint32_t       total      = 0;       // linear: length+latency; wrapped: warmup+length
    uint32_t       start      = 0;       // wrapped: loop anchor (playhead + margin)
    uint32_t       warmup     = 0;       // wrapped: discarded left-context priming feeds
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

// Step 3 wrapped re-render block. The buffer is rendered in cyclic order anchored at
// `start` (just ahead of the playhead) so the looping audio reaches fresh content
// quickly without a mute. Feed steps [base,base+n) of a `warmup + length` schedule:
//   - F < warmup: prime the (freshly Reset) chain with left-context from
//     source[start-warmup .. start); outputs discarded.
//   - F >= warmup: feed source[start + (F-warmup)] cyclically and write the
//     latency-compensated output to dst[(start + (F-warmup) - latency) % length],
//     mirroring RenderStreamBlock_'s dst = source-read - latency invariant. The
//     warmup (>= latency) means these outputs are already valid and clean.
// Indices are modular over `length` (the loop); src and dst are the same buffer's
// worth of frames. Caller guarantees length > latency.
void RenderWrappedBlock_(const int16_t* src,
                         uint32_t       length,
                         uint32_t       latency,
                         uint32_t       start,
                         uint32_t       warmup,
                         uint32_t       base,
                         uint32_t       n,
                         int16_t*       dst)
{
    float fbuf[256];
    for(uint32_t i = 0; i < n; ++i)
    {
        const uint32_t F = base + i;
        const uint32_t src_step = (F < warmup) ? (length - warmup + F)  // start-warmup+F
                                               : (F - warmup);          // start + main step
        const uint32_t srcIdx = (start + src_step) % length;
        fbuf[i] = static_cast<float>(src[srcIdx]) * (1.0f / 32768.0f);
    }
    s_render_chain.Process(fbuf, n);
    for(uint32_t i = 0; i < n; ++i)
    {
        const uint32_t F = base + i;
        if(F < warmup)
            continue; // priming: discard
        const uint32_t f      = F - warmup;                       // main feed step 0..length
        const uint32_t dstIdx = (start + f + length - latency) % length; // (start+f-latency) mod length
        float v = fbuf[i];
        if(v > 1.0f) v = 1.0f;
        else if(v < -1.0f) v = -1.0f;
        int32_t s = static_cast<int32_t>(v * 32767.0f);
        if(s > 32767) s = 32767;
        else if(s < -32768) s = -32768;
        dst[dstIdx] = static_cast<int16_t>(s);
    }
}

// Publish the result and hand off to the audio thread for dry auto-play. When
// use_source is true (no active effect) we play the unprocessed source.
void FinalizePreview(AppUiState& ui, AppSharedState& shared, bool use_source)
{
    (void)ui;
    auto&   bake = shared.bake_preview;
    Sample& rs   = bake.render_sample;
    // Session was stopped (user left CRAFT) while this render was in flight: discard
    // it — don't start/continue playback on another screen. The audio is already
    // stopping/stopped via stop_req.
    if(bake.craft_preview_wanted.load(std::memory_order_acquire) == 0u)
    {
        bake.craft_render_active.store(0, std::memory_order_release);
        s_preview.active = false;
        return;
    }
    // A re-render of an already-looping CRAFT audition (Step 2): the rendered
    // buffer pointer is unchanged (always SdManageBuffer), we just rewrote its
    // contents in place. Don't re-issue start_req — that would reset the playhead
    // to 0 and chop the loop. Let the audio keep looping; it was muted during the
    // render (craft_render_active gate) and unmutes here onto the fresh content.
    // use_source repoints to a DIFFERENT buffer (raw source), so it must restart.
    const bool already_looping = (bake.active.load(std::memory_order_acquire) != 0u)
                                 && (bake.play_render.load(std::memory_order_acquire) != 0u);
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
    if(!already_looping || use_source)
        bake.start_req.store(1, std::memory_order_release);   // initial auto-play / buffer switch
    // Drop the breathe + unmute (craft_render_active gates the audio mute). dirty
    // was already cleared at render START (BeginCraftRenderPreview) so a mid-render
    // edit re-dirties and the worker fires a follow-up re-render.
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

bool CraftConfigHasLatency(const AppUiState& ui)
{
    // Only STFT effects (`fresh`, `thru`) add latency. A config without one is
    // zero-latency and can audition LIVE on the audio thread (A1); a config with one
    // still uses the worker render path until engine gating lands (A2).
    for(uint8_t s = 0; s < craft::kCraftSlotCount; ++s)
    {
        const uint8_t plugin = ui.craft_slot_plugin[s] % craft::kCraftPluginCount;
        if(plugin == craft::kCraftPluginFresh || plugin == craft::kCraftPluginThru
           || plugin == craft::kCraftPluginZero || plugin == craft::kCraftPluginRand)
            return true;
    }
    return false;
}

void PublishCraftCfgLive(const AppUiState& ui, AppSharedState& shared)
{
    // Seqlock publish (odd = writing, even = stable) so the audio thread re-applies
    // coeffs without resetting chain state — smooth live param edits.
    const craft::CraftChainConfig cfg = BuildConfigFromUi(ui);
    auto&          bake = shared.bake_preview;
    const uint32_t s    = bake.craft_cfg_seq.load(std::memory_order_relaxed);
    bake.craft_cfg_seq.store(s + 1u, std::memory_order_release); // odd: writing
    bake.craft_cfg = cfg;
    bake.craft_cfg_seq.store(s + 2u, std::memory_order_release); // even: stable
}

bool CraftPreviewStartLive(AppUiState& ui, AppSharedState& shared)
{
    const Sample& src = shared.bake_preview.sample;
    if(src.pcm == nullptr || src.length == 0u)
        return false;
    auto& bake = shared.bake_preview;
    PublishCraftCfgLive(ui, shared);
    // play_render=0 => audio plays `sample` (the source) through the live chain;
    // craft_chain_active=1 => audio runs the chain block-by-block, looping.
    bake.play_render.store(0, std::memory_order_release);
    bake.craft_chain_active.store(1, std::memory_order_release);
    bake.craft_preview_wanted.store(1, std::memory_order_release);
    bake.start_req.store(1, std::memory_order_release);
    ui.craft_preview_dirty = false; // live = always matches config
    return true;
}

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
    // Snapshot taken: clear dirty HERE (not at finalize) so a param edit DURING
    // this render re-sets craft_preview_dirty and the worker auto-fires a fresh
    // re-render with the latest values when this one finishes. Coalesces fast
    // knob spins into one-in-flight without ever dropping the final value.
    ui.craft_preview_dirty = false;

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
    s_preview.dst     = SdManageBuffer();
    if(s_preview.has_effect && s_preview.dst == nullptr)
        return false;

    // Step 3: if a rendered audition is ALREADY looping, this is a re-render — do it
    // in WRAPPED order anchored ahead of the live playhead, so the change is heard in
    // ~margin with no mute. The initial render (nothing looping yet) stays on the
    // simple linear-from-0 path. STFT-less or tiny (length<=latency) renders also use
    // linear (wrapped needs real latency + headroom).
    auto&          bake           = shared.bake_preview;
    const bool     already_looping = (bake.active.load(std::memory_order_acquire) != 0u)
                                     && (bake.play_render.load(std::memory_order_acquire) != 0u);
    if(already_looping && s_preview.has_effect && length > s_preview.latency)
    {
        uint32_t margin = kPreviewReRenderMargin;
        if(margin >= length)
            margin = length / 2u;
        uint32_t warm = 2u * s_preview.latency;
        if(warm > length)
            warm = length;
        const uint32_t playhead = bake.pos.load(std::memory_order_acquire) % length;
        s_preview.wrapped = true;
        s_preview.start   = (playhead + margin) % length;
        s_preview.warmup  = warm;
        s_preview.total   = warm + length;
    }
    else
    {
        s_preview.wrapped = false;
        s_preview.total   = length + s_preview.latency;
    }

    s_preview.start_ms = daisy::System::GetNow();
    s_preview.active   = true;
    shared.bake_preview.craft_preview_wanted.store(1, std::memory_order_release);
    shared.bake_preview.craft_render_active.store(1, std::memory_order_release);
    return true;
}

bool StepCraftRenderPreview(AppUiState& ui, AppSharedState& shared)
{
    if(!s_preview.active)
        return true;

    // No active effect: nothing to render — switch straight to the dry source
    // (no breathe/dead-space).
    if(!s_preview.has_effect)
    {
        FinalizePreview(ui, shared, /*use_source=*/true);
        return true;
    }

    // Render flat out: one kRenderStepCap chunk per worker tick. The cap keeps each
    // tick short so the UI stays responsive. The wrapped (Step 3) path renders ahead
    // of the live playhead so there's no mute; the linear path is the initial render.
    const uint32_t step_end = (s_preview.pos + kRenderStepCap < s_preview.total)
                                  ? (s_preview.pos + kRenderStepCap)
                                  : s_preview.total;

    constexpr uint32_t kBlock = 256u;
    for(uint32_t base = s_preview.pos; base < step_end; base += kBlock)
    {
        const uint32_t n = (step_end - base < kBlock) ? (step_end - base) : kBlock;
        if(s_preview.wrapped)
            RenderWrappedBlock_(s_preview.src, s_preview.length, s_preview.latency,
                                s_preview.start, s_preview.warmup, base, n, s_preview.dst);
        else
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
