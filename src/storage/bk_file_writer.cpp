#include "bk_file_writer.h"

#include "ff.h"
#include "mem_regions.h"

#include <cstring>

// IMPORTANT: SDMMC DMA on this STM32H7 cannot reach DTCM (where the main-loop
// stack lives). f_write returns FR_OK on unreachable buffers but writes 0
// real bytes — failure mode is a 0-byte file. THREE things must live in
// DMA-reachable memory:
//
//   1. The header bytes handed to f_write — the caller's header lives on
//      whatever stack they're on (likely DTCM), so we copy into s_bk_hdr_buf
//      before writing.
//   2. The silence-fill source for nullptr slices.
//   3. The FIL struct itself — FATFS's internal `buf[FF_MAX_SS]` sector
//      cache lives inside FIL and is DMA-touched on partial sector I/O.
//      A stack-local FIL silently corrupts those reads/writes.
//
// SECTION CHOICE: The header buf and FIL live in `.sdram_bss` (SDRAM,
// DMA-reachable). Their initial state doesn't matter — both are overwritten
// before use. The silence chunk MUST live in default `.bss` (AXI SRAM, also
// DMA-reachable) because `.sdram_bss` is NOLOAD — the C runtime startup
// does NOT zero-init it, so a `{}` initializer is a no-op there. AXI SRAM
// `.bss` IS zeroed at startup, so the chunk reliably contains zeros without
// any explicit memset.
ADSR2_SECTION(".sdram_bss") static bk::BkFileHeader s_bk_hdr_buf{};
ADSR2_SECTION(".sdram_bss") static FIL              s_bk_file;
static constexpr uint32_t kBkZeroChunkFrames = 512u;
static int16_t s_bk_zero_chunk[kBkZeroChunkFrames] = {};

namespace {

// Stream `frames_per_slice` worth of zero samples in 512-frame chunks.
// Used by both WriteSlice(nullptr) and End's silence-padding.
bool WriteSilenceSlice_(uint32_t frames_per_slice)
{
    uint32_t frames_remaining = frames_per_slice;
    while(frames_remaining > 0u)
    {
        const uint32_t chunk_frames
            = (frames_remaining > kBkZeroChunkFrames) ? kBkZeroChunkFrames
                                                      : frames_remaining;
        const UINT chunk_bytes = static_cast<UINT>(chunk_frames * sizeof(int16_t));
        UINT chunk_bw = 0;
        if(f_write(&s_bk_file, s_bk_zero_chunk, chunk_bytes, &chunk_bw) != FR_OK
           || chunk_bw != chunk_bytes)
            return false;
        frames_remaining -= chunk_frames;
    }
    return true;
}

} // namespace

namespace bk {

BkFileHeader MakeDefaultHeader()
{
    BkFileHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    std::memcpy(hdr.magic, kMagic, sizeof(kMagic));
    hdr.version                 = kVersion;
    hdr.reserved0               = 0u;
    hdr.sample_rate             = 48000u;
    hdr.channels                = 1u;
    hdr.bits_per_sample         = 16u;
    hdr.source_duration_samples = 0u;   // caller fills
    hdr.root_midi_note          = 60u;  // C4 default
    hdr.lo_note                 = kLoNote;
    hdr.hi_note                 = kHiNote;
    hdr.algorithm_id            = static_cast<uint8_t>(kAlgorithmSilence);
    // source_name + reserved1 already zeroed by memset
    return hdr;
}

bool BkWriter_Begin(BkWriter&           w,
                    const char*         path,
                    const BkFileHeader& hdr,
                    uint32_t            frames_per_slice)
{
    w.open             = false;
    w.frames_per_slice = 0;
    w.slice_bytes      = 0;
    w.slices_total     = 0;
    w.slot_cursor      = 0;

    if(path == nullptr || path[0] == '\0')
        return false;
    if(frames_per_slice == 0u)
        return false;
    if(hdr.source_duration_samples != frames_per_slice)
        return false;
    const uint32_t expected_slices
        = static_cast<uint32_t>(hdr.hi_note - hdr.lo_note + 1u);

    std::memset(&s_bk_file, 0, sizeof(s_bk_file));
    if(f_open(&s_bk_file, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return false;

    // Header into SDRAM-resident static, then write.
    s_bk_hdr_buf = hdr;
    UINT bw = 0;
    if(f_write(&s_bk_file, &s_bk_hdr_buf, sizeof(s_bk_hdr_buf), &bw) != FR_OK
       || bw != sizeof(s_bk_hdr_buf))
    {
        f_close(&s_bk_file);
        f_unlink(path);
        return false;
    }

    w.frames_per_slice = frames_per_slice;
    w.slice_bytes      = frames_per_slice * sizeof(int16_t);
    w.slices_total     = expected_slices;
    w.slot_cursor      = 0;
    w.open             = true;
    return true;
}

bool BkWriter_WriteSlice(BkWriter& w, const int16_t* src)
{
    if(!w.open)
        return false;
    if(w.slot_cursor >= w.slices_total)
        return false;

    if(src != nullptr)
    {
        UINT bw = 0;
        if(f_write(&s_bk_file, src, w.slice_bytes, &bw) != FR_OK
           || bw != w.slice_bytes)
            return false;
    }
    else
    {
        if(!WriteSilenceSlice_(w.frames_per_slice))
            return false;
    }
    ++w.slot_cursor;
    return true;
}

bool BkWriter_End(BkWriter& w, const char* path, BkWriteProgressCb cb)
{
    if(!w.open)
    {
        if(path != nullptr && path[0] != '\0')
            f_unlink(path);
        return false;
    }

    // Pad any remaining slots with silence so the file is always
    // slices_total slots long, regardless of caller's emission count.
    const uint32_t pad_total = (w.slot_cursor < w.slices_total)
                                   ? (w.slices_total - w.slot_cursor)
                                   : 0u;
    uint32_t pad_done = 0u;
    bool     ok       = true;
    while(ok && w.slot_cursor < w.slices_total)
    {
        ok = WriteSilenceSlice_(w.frames_per_slice);
        if(ok)
        {
            ++w.slot_cursor;
            ++pad_done;
            if(cb != nullptr)
                cb(pad_done, pad_total);
        }
    }

    if(f_close(&s_bk_file) != FR_OK)
        ok = false;

    w.open = false;
    if(!ok && path != nullptr && path[0] != '\0')
        f_unlink(path);
    return ok;
}

bool BkWrite_File(const char*           path,
                  const BkFileHeader&   hdr,
                  const int16_t* const* slice_ptrs,
                  uint32_t              slice_count,
                  uint32_t              frames_per_slice,
                  BkWriteProgressCb     cb)
{
    if(slice_count == 0u)
        return false;
    const uint32_t expected_slices
        = static_cast<uint32_t>(hdr.hi_note - hdr.lo_note + 1u);
    if(slice_count != expected_slices)
        return false;

    BkWriter w;
    if(!BkWriter_Begin(w, path, hdr, frames_per_slice))
        return false;

    for(uint32_t i = 0u; i < slice_count; ++i)
    {
        const int16_t* src = (slice_ptrs != nullptr) ? slice_ptrs[i] : nullptr;
        if(!BkWriter_WriteSlice(w, src))
        {
            BkWriter_End(w, path); // unlinks
            return false;
        }
        if(cb != nullptr)
            cb(i + 1u, slice_count);
    }

    return BkWriter_End(w, path);
}

} // namespace bk
