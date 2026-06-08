#include "ui_worker_internal.h"
#include "sample_style.h"

#include <cstdio>
#include <cstring>

// Byte-order helpers stay local to the WAV helper unit.
static uint16_t ReadU16LE(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t ReadU32LE(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static void WriteU16LE(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}

static void WriteU32LE(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

bool ParseWavHeader(FIL& file, WavInfo& info)
{
    uint8_t hdr[12];
    UINT br = 0;
    if(f_lseek(&file, 0) != FR_OK)
        return false;
    if(f_read(&file, hdr, sizeof(hdr), &br) != FR_OK || br != sizeof(hdr))
        return false;
    if(std::memcmp(hdr, "RIFF", 4) != 0)
        return false;
    if(std::memcmp(hdr + 8, "WAVE", 4) != 0)
        return false;

    bool have_fmt = false;
    bool have_data = false;
    for(int chunk = 0; chunk < 16 && !have_data; ++chunk)
    {
        uint8_t chdr[8];
        if(f_read(&file, chdr, sizeof(chdr), &br) != FR_OK || br != sizeof(chdr))
            return false;
        const uint32_t csize = ReadU32LE(chdr + 4);
        const uint32_t data_start = f_tell(&file);

        if(std::memcmp(chdr, "fmt ", 4) == 0)
        {
            uint8_t fmt[16];
            if(csize < sizeof(fmt))
                return false;
            if(f_read(&file, fmt, sizeof(fmt), &br) != FR_OK || br != sizeof(fmt))
                return false;
            info.audio_format = ReadU16LE(fmt + 0);
            info.channels = ReadU16LE(fmt + 2);
            info.sample_rate = ReadU32LE(fmt + 4);
            info.bits_per_sample = ReadU16LE(fmt + 14);
            have_fmt = true;

            uint32_t remaining = csize - sizeof(fmt);
            if(remaining > 0)
                f_lseek(&file, data_start + csize);
        }
        else if(std::memcmp(chdr, "data", 4) == 0)
        {
            info.data_offset = data_start;
            info.data_size = csize;
            have_data = true;
            break;
        }
        else
        {
            f_lseek(&file, data_start + csize);
        }

        if(csize & 1u)
            f_lseek(&file, f_tell(&file) + 1);
    }

    return have_fmt && have_data;
}

bool WriteWavHeader(FIL& file,
                    uint32_t frames,
                    FRESULT* out_res,
                    UINT* out_bw,
                    uint32_t* out_requested)
{
    const uint32_t data_bytes = frames * 2u;
    const uint32_t riff_size = 36u + data_bytes;
    uint8_t hdr[44];
    std::memset(hdr, 0, sizeof(hdr));
    std::memcpy(hdr + 0, "RIFF", 4);
    WriteU32LE(hdr + 4, riff_size);
    std::memcpy(hdr + 8, "WAVE", 4);
    std::memcpy(hdr + 12, "fmt ", 4);
    WriteU32LE(hdr + 16, 16u);
    WriteU16LE(hdr + 20, 1u);
    WriteU16LE(hdr + 22, 1u);
    WriteU32LE(hdr + 24, 48000u);
    WriteU32LE(hdr + 28, 48000u * 2u);
    WriteU16LE(hdr + 32, 2u);
    WriteU16LE(hdr + 34, 16u);
    std::memcpy(hdr + 36, "data", 4);
    WriteU32LE(hdr + 40, data_bytes);

    if(out_requested)
        *out_requested = sizeof(hdr);

    FRESULT res = f_lseek(&file, 0);
    if(out_res)
        *out_res = res;
    if(out_bw)
        *out_bw = 0;
    if(res != FR_OK)
        return false;

    UINT bw = 0;
    res = f_write(&file, hdr, sizeof(hdr), &bw);
    if(out_res)
        *out_res = res;
    if(out_bw)
        *out_bw = bw;
    if(res != FR_OK || bw != sizeof(hdr))
        return false;
    return true;
}

bool IsWavName(const char* name)
{
    if(!name)
        return false;
    const size_t len = std::strlen(name);
    if(len < 4)
        return false;
    const char c0 = name[len - 4];
    const char c1 = name[len - 3];
    const char c2 = name[len - 2];
    const char c3 = name[len - 1];
    return (c0 == '.' && (c1 == 'W' || c1 == 'w') && (c2 == 'A' || c2 == 'a')
            && (c3 == 'V' || c3 == 'v'));
}

bool IsBkName(const char* name)
{
    if(!name)
        return false;
    const size_t len = std::strlen(name);
    if(len < 3)
        return false;
    const char c0 = name[len - 3];
    const char c1 = name[len - 2];
    const char c2 = name[len - 1];
    return (c0 == '.' && (c1 == 'B' || c1 == 'b') && (c2 == 'K' || c2 == 'k'));
}

bool MakePath(char* out, size_t n, const char* base, const char* name)
{
    if(!out || n == 0 || !base || !name)
        return false;

    const size_t blen = std::strlen(base);
    const bool has_sep = (blen > 0 && base[blen - 1] == '/');
    int r = 0;
    if(has_sep)
        r = std::snprintf(out, n, "%s%s", base, name);
    else
        r = std::snprintf(out, n, "%s/%s", base, name);

    if(r < 0 || r >= static_cast<int>(n))
    {
        out[n - 1] = '\0';
        return false;
    }
    return true;
}

void WorkerExtractBaseName(const char* path, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;
    out[0] = '\0';
    if(!path || path[0] == '\0')
        return;
    BuildSampleDisplayName(path, out, out_n);
}
