#include "bk_file_reader.h"

#include "ff.h"
#include "mem_regions.h"

#include <cstring>

// Same SDMMC-DMA-cannot-reach-DTCM constraint as the writer. f_read into a
// DTCM stack buffer silently produces garbage rather than an FRESULT error
// — symptom is "valid file fails validation." All three reader scratch
// buffers must live in DMA-reachable memory.
//
// Header buffer and FIL go to `.sdram_bss` (overwritten before use; initial
// state doesn't matter). The header buffer doubles as the read destination
// before we copy out to the caller. The reader is single-threaded by
// contract (called from main or worker, never from audio).
ADSR2_SECTION(".sdram_bss") static bk::BkFileHeader s_bk_read_hdr_buf{};
ADSR2_SECTION(".sdram_bss") static FIL              s_bk_read_file;

namespace bk {

namespace {

bool ValidateHeader(const BkFileHeader& hdr)
{
    if(std::memcmp(hdr.magic, kMagic, sizeof(kMagic)) != 0)
        return false;
    if(hdr.version != kVersion)
        return false;
    if(hdr.sample_rate != 48000u)
        return false;
    if(hdr.channels != 1u)
        return false;
    if(hdr.bits_per_sample != 16u)
        return false;
    if(hdr.source_duration_samples == 0u)
        return false;
    if(hdr.lo_note > hdr.hi_note)
        return false;
    return true;
}

} // namespace

bool BkRead_OpenAndValidateHeader(const char* path, BkFileHeader& out_hdr)
{
    if(path == nullptr || path[0] == '\0')
        return false;

    // SDRAM-resident FIL + header scratch (see top-of-file comment).
    std::memset(&s_bk_read_file, 0, sizeof(s_bk_read_file));
    if(f_open(&s_bk_read_file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return false;

    UINT br = 0;
    const FRESULT res
        = f_read(&s_bk_read_file, &s_bk_read_hdr_buf, sizeof(s_bk_read_hdr_buf), &br);
    f_close(&s_bk_read_file);
    if(res != FR_OK || br != sizeof(s_bk_read_hdr_buf))
        return false;
    if(!ValidateHeader(s_bk_read_hdr_buf))
        return false;

    // Copy validated header out to the caller (caller's storage may be on
    // a DTCM stack — that's fine, we're not DMAing into it, just memcpying).
    out_hdr = s_bk_read_hdr_buf;
    return true;
}

bool BkRead_LoadIntoBuffer(const char*   path,
                           int16_t*      dst_buf,
                           uint32_t      dst_max_frames,
                           BkFileHeader& out_hdr)
{
    if(path == nullptr || path[0] == '\0' || dst_buf == nullptr || dst_max_frames == 0u)
        return false;

    std::memset(&s_bk_read_file, 0, sizeof(s_bk_read_file));
    if(f_open(&s_bk_read_file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return false;

    UINT br = 0;
    if(f_read(&s_bk_read_file, &s_bk_read_hdr_buf, sizeof(s_bk_read_hdr_buf), &br) != FR_OK
       || br != sizeof(s_bk_read_hdr_buf))
    {
        f_close(&s_bk_read_file);
        return false;
    }
    if(!ValidateHeader(s_bk_read_hdr_buf))
    {
        f_close(&s_bk_read_file);
        return false;
    }

    const uint32_t pcm_bytes = PcmBlobBytes(s_bk_read_hdr_buf);
    const uint32_t pcm_frames = pcm_bytes / sizeof(int16_t);
    if(pcm_frames > dst_max_frames)
    {
        f_close(&s_bk_read_file);
        return false;
    }

    // Stream the PCM blob directly into the caller's destination buffer.
    // CALLER is responsible for ensuring dst_buf is DMA-reachable (SDRAM
    // or AXI-SRAM). Stage 2+ will pass an SDRAM bake-preview-or-similar
    // buffer; the .bk slice player will use a dedicated SDRAM region.
    // 32 KB chunks — safe middle ground for FatFS f_read.
    constexpr uint32_t kChunkBytes = 32u * 1024u;
    uint32_t bytes_remaining = pcm_bytes;
    uint8_t* dst             = reinterpret_cast<uint8_t*>(dst_buf);
    while(bytes_remaining > 0u)
    {
        const UINT chunk
            = static_cast<UINT>((bytes_remaining > kChunkBytes) ? kChunkBytes : bytes_remaining);
        UINT chunk_br = 0;
        if(f_read(&s_bk_read_file, dst, chunk, &chunk_br) != FR_OK || chunk_br != chunk)
        {
            f_close(&s_bk_read_file);
            return false;
        }
        dst += chunk;
        bytes_remaining -= chunk;
    }

    f_close(&s_bk_read_file);
    out_hdr = s_bk_read_hdr_buf;
    return true;
}

} // namespace bk
