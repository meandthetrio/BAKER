#include "ui_worker_internal.h"

#include "app_state_shared.h"
#include "bk_file_format.h"
#include "bk_file_reader.h"
#include "bk_layer_load.h"
#include "mem_regions.h"
#include "sd_browser_state.h"
#include "sd_sample_pool.h"

#include <cstdio>
#include <cstring>

using namespace daisy;

// Async .bk multisample loader (UiReqType::LoadBkIndex). Streams the WHOLE file
// from offset 0 into g_sd_bk_layer_b_buf, one f_read (one SDMMC IDMA transfer) per
// worker tick, yielding to the main loop between ticks — exactly the pattern the
// WAV loaders use. The old synchronous loader read the blob in a tight back-to-back
// loop and hard-failed (FR_DISK_ERR) after a project load; issuing one transfer per
// tick avoids that. Reading from offset 0 also keeps every direct DMA sector-aligned
// (reading the PCM alone starts mid-sector -> a misaligned DMA that also fails). The
// header ends up in front of the PCM in the buffer, so finalize offsets the slices
// by the header size (no giant memmove needed).

namespace
{
// Header captured at start (SDRAM: it is filled by BkRead_OpenAndValidateHeader,
// which memcpys — no DMA into it here), used at completion to build slices.
ADSR2_SECTION(".sdram_bss") bk::BkFileHeader s_bk_ld_hdr{};

// 64 KB = 128 sectors = the STM32H7 SDMMC IDMA per-transfer maximum. A larger
// f_read would make FatFs split it into back-to-back DMAs in one call — the very
// pattern that hard-fails after a project load — so this is the ceiling for the
// "one DMA per worker tick" scheme. Read a full 64 KB every tick (except the last).
constexpr uint32_t kBkChunkBytesMax = 64u * 1024u;
constexpr uint32_t kBkChunkBytesMin = 64u * 1024u;
} // namespace

// Begin an async .bk load of sd.paths[index] into layer B. Validates the header,
// opens the file for streaming, and arms the per-tick step. Returns false (and sets
// a status) if it can't start.
bool StartBkLoadInternal(SdBrowserState& sd, AppSharedState& shared, uint16_t index)
{
    s_sd.state       = LoaderState::Idle;
    sd.load_pending  = false;

    if(!EnsureSdMountedInternal(sd))
        return false;
    if(index >= sd.wav_count)
    {
        SdBrowser_SetStatus(sd, "BAD IDX");
        return false;
    }

    const char* path = sd.paths[index];

    // Validate the header + get dimensions (opens/closes its own FIL).
    if(!bk::BkRead_OpenAndValidateHeader(path, s_bk_ld_hdr))
    {
        SdBrowser_SetStatus(sd, "BK ERR");
        return false;
    }

    const uint32_t total_bytes = bk::TotalFileBytes(s_bk_ld_hdr); // header + PCM
    // The buffer briefly holds header + PCM (we read the whole file).
    if((total_bytes / sizeof(int16_t)) > kBkLayerBMaxFrames)
    {
        SdBrowser_SetStatus(sd, "BK SIZE");
        return false;
    }

    // Open for streaming from offset 0.
    if(f_open(&s_sd.file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
    {
        SdBrowser_SetStatus(sd, "BK OPEN");
        return false;
    }

    // EngineRefreshLoadedMetadata rebuilds the engine-screen name from this on the
    // sd_applied_gen bump we fire at completion, so it must point at the .bk.
    std::snprintf(sd.last_loaded_path, sizeof(sd.last_loaded_path), "%s", path);

    // Stop any currently-sounding .bk while its buffer is overwritten by DMA.
    shared.bk_layer_b.loaded = false;

    s_sd.file_open    = true;
    s_sd.data_size    = total_bytes;
    s_sd.bytes_loaded = 0u;
    s_sd.load_index   = index;
    s_sd.load_target  = LoadTarget::LiveSlot;
    s_sd.state        = LoaderState::Load;

    sd.load_in_progress = true;
    sd.load_progress    = 0;
    SdWavLoad_SetBusy(shared, sd, true);
    SdBrowser_SetStatus(sd, "LOADING");
    return true;
}

// One step of the async .bk load: reads a single sector-aligned chunk, or finalizes
// when the whole file is in the buffer. Returns true when the request is complete
// (success or error), false to be called again next tick.
bool BkLoadStepInternal(SdBrowserState& sd, AppSharedState& shared, uint16_t budget)
{
    if(!s_sd.file_open)
        return true;

    const uint32_t remaining
        = (s_sd.data_size > s_sd.bytes_loaded) ? (s_sd.data_size - s_sd.bytes_loaded) : 0u;

    if(remaining > 0u)
    {
        // One f_read (one IDMA transfer) per tick. Round the byte budget to a
        // multiple of 512 so each chunk's file offset stays sector-aligned (the
        // last, partial chunk excepted — its tail sector goes via FatFs's window).
        uint32_t want = budget;
        if(want < kBkChunkBytesMin)
            want = kBkChunkBytesMin;
        if(want > kBkChunkBytesMax)
            want = kBkChunkBytesMax;
        want &= ~static_cast<uint32_t>(0x1FFu);
        const UINT to_read = static_cast<UINT>((remaining > want) ? want : remaining);

        uint8_t* dst = reinterpret_cast<uint8_t*>(SdBkLayerBBuffer()) + s_sd.bytes_loaded;
        UINT     br  = 0;
        if(f_read(&s_sd.file, dst, to_read, &br) != FR_OK || br == 0u)
        {
            f_close(&s_sd.file);
            s_sd.file_open      = false;
            s_sd.state          = LoaderState::Idle;
            sd.load_in_progress = false;
            SdWavLoad_SetBusy(shared, sd, false);
            shared.bk_layer_b.loaded = false;
            sd.wav_err_count++;
            SdBrowser_SetStatus(sd, "BK READ");
            return true; // done (error)
        }

        s_sd.bytes_loaded += br;
        uint32_t pct = (s_sd.bytes_loaded * 100u) / s_sd.data_size;
        if(pct > 100u)
            pct = 100u;
        sd.load_progress = static_cast<uint8_t>(pct);

        if(s_sd.bytes_loaded < s_sd.data_size)
            return false; // more to read next tick
    }

    // Whole file is in the buffer as [header | PCM]. Build the slice handles
    // pointing at the PCM (offset = header frames) — no memmove needed.
    f_close(&s_sd.file);
    s_sd.file_open = false;
    s_sd.state     = LoaderState::Idle;

    constexpr uint32_t kPcmOffsetFrames
        = static_cast<uint32_t>(sizeof(bk::BkFileHeader)) / sizeof(int16_t);
    const bool ok = bk::BkLayer_FinalizeLayerB(shared, s_bk_ld_hdr, kPcmOffsetFrames);

    if(ok)
    {
        // Fire the same handoff signals the WAV loader does (ui_worker_sample_load
        // completion) so AudioCallback_ApplySdSampleHandoffs applies slot 0 and
        // bumps sd_applied_gen — which is what makes EngineRefreshLoadedMetadata
        // refresh the engine-screen name + waveform preview and re-render. Without
        // these the .bk audio plays (via the bk_layer_b override) but the UI stays
        // stale (old preview / "no sample").
        shared.sample.edit.sd_edit_pending = shared.sample.edit.sd_edit_slots[0];
        shared.sample.edit.sd_edit_slot.store(0, std::memory_order_release);
        shared.sample.edit.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
        shared.sample.edit.sd_edit_ready.store(1, std::memory_order_release);
        shared.sample.publish.sd_published_gen.fetch_add(1, std::memory_order_acq_rel);
        shared.sample.publish.sd_published_slot.store(0, std::memory_order_release);
        shared.sample.publish.sd_published_ready.store(1, std::memory_order_release);
    }

    sd.load_in_progress = false;
    sd.load_progress    = 100;
    SdWavLoad_SetBusy(shared, sd, false);
    SdBrowser_SetStatus(sd, ok ? "LOADED" : "BK ERR");
    return true;
}
