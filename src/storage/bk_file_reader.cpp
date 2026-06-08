#include "bk_file_reader.h"

#include "ff.h"

#include <cstring>

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

    FIL file{};
    if(f_open(&file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return false;

    BkFileHeader hdr;
    UINT br = 0;
    const FRESULT res = f_read(&file, &hdr, sizeof(hdr), &br);
    f_close(&file);
    if(res != FR_OK || br != sizeof(hdr))
        return false;
    if(!ValidateHeader(hdr))
        return false;

    out_hdr = hdr;
    return true;
}

bool BkRead_LoadIntoBuffer(const char*   path,
                           int16_t*      dst_buf,
                           uint32_t      dst_max_frames,
                           BkFileHeader& out_hdr)
{
    if(path == nullptr || path[0] == '\0' || dst_buf == nullptr || dst_max_frames == 0u)
        return false;

    FIL file{};
    if(f_open(&file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return false;

    BkFileHeader hdr;
    UINT br = 0;
    if(f_read(&file, &hdr, sizeof(hdr), &br) != FR_OK || br != sizeof(hdr))
    {
        f_close(&file);
        return false;
    }
    if(!ValidateHeader(hdr))
    {
        f_close(&file);
        return false;
    }

    const uint32_t pcm_bytes = PcmBlobBytes(hdr);
    const uint32_t pcm_frames = pcm_bytes / sizeof(int16_t);
    if(pcm_frames > dst_max_frames)
    {
        f_close(&file);
        return false;
    }

    // Stream the PCM blob in chunks — FatFS f_read for big buffers can be
    // slow / hit driver limits. 32 KB chunk is a safe middle ground.
    constexpr uint32_t kChunkBytes = 32u * 1024u;
    uint32_t bytes_remaining = pcm_bytes;
    uint8_t* dst             = reinterpret_cast<uint8_t*>(dst_buf);
    while(bytes_remaining > 0u)
    {
        const UINT chunk
            = static_cast<UINT>((bytes_remaining > kChunkBytes) ? kChunkBytes : bytes_remaining);
        UINT chunk_br = 0;
        if(f_read(&file, dst, chunk, &chunk_br) != FR_OK || chunk_br != chunk)
        {
            f_close(&file);
            return false;
        }
        dst += chunk;
        bytes_remaining -= chunk;
    }

    f_close(&file);
    out_hdr = hdr;
    return true;
}

} // namespace bk
