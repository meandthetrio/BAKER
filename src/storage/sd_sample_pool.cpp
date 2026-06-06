#include "sd_sample_pool.h"

#include "mem_regions.h"

#include <cstring>

// Playback slot 0 lives in fast internal RAM_D2; slot 1 stays in SDRAM. Splitting
// the former 2-D array lets the two slots use different sections and sizes.
ADSR2_SECTION(".ram_d2_bss") ADSR2_ALIGN32 static int16_t g_sd_sample_buf0[kSdPlaybackD2MaxFrames];
ADSR2_SECTION(".sdram_bss")  ADSR2_ALIGN32 static int16_t g_sd_sample_buf1[kSdSampleMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_record_buf[kSdSampleMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_manage_buf[kSdSampleMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_baked_buf[kSdSampleSlots][kSdSampleMaxFrames];
// SDRAM staging buffer for slot 0: SDMMC DMA can write here (D2 SRAM it cannot),
// then SdSampleLoadCommit() copies into the RAM_D2 playback buffer with the CPU.
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_d2_stage[kSdPlaybackD2MaxFrames];

int16_t* SdSampleBuffer(uint8_t slot)
{
    return (slot == 0u) ? g_sd_sample_buf0 : g_sd_sample_buf1;
}

int16_t* SdSampleLoadBuffer(uint8_t slot)
{
    return (slot == 0u) ? g_sd_d2_stage : g_sd_sample_buf1;
}

void SdSampleLoadCommit(uint8_t slot, uint32_t frames)
{
    if(slot != 0u)
        return; // slot 1 loaded straight into its playback buffer
    if(frames > kSdPlaybackD2MaxFrames)
        frames = kSdPlaybackD2MaxFrames;
    // CPU copy SDRAM stage -> RAM_D2 playback buffer. Same-core read-after-write,
    // so no cache maintenance is needed (no DMA touches the RAM_D2 buffer).
    std::memcpy(g_sd_sample_buf0, g_sd_d2_stage, static_cast<size_t>(frames) * sizeof(int16_t));
}

int16_t* SdRecordBuffer()
{
    return g_sd_record_buf;
}

int16_t* SdManageBuffer()
{
    return g_sd_manage_buf;
}

int16_t* SdBakedBuffer(uint8_t slot)
{
    if(slot >= kSdSampleSlots)
        slot = 0;
    return g_sd_baked_buf[slot];
}

uint32_t SdSampleMaxFrames(uint8_t slot)
{
    return (slot == 0u) ? kSdPlaybackD2MaxFrames : kSdSampleMaxFrames;
}
