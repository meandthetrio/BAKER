#include "sd_sample_pool.h"

#include "mem_regions.h"

#include <cstring>

// Playback slot 0 lives in fast internal RAM_D2; slot 1 stays in SDRAM. Splitting
// the former 2-D array lets the two slots use different sections and sizes.
ADSR2_SECTION(".ram_d2_bss") ADSR2_ALIGN32 static int16_t g_sd_sample_buf0[kSdPlaybackD2MaxFrames];
ADSR2_SECTION(".sdram_bss")  ADSR2_ALIGN32 static int16_t g_sd_sample_buf1[kSdSampleMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_record_buf[kSdSampleMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_manage_buf[kSdSampleMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_bake_preview_buf[kSdSampleMaxFrames];
// ~40 MB. Largest single SDRAM allocation in the project, but well within
// budget (64 MB SDRAM total, ~14 MB already used).
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_bk_layer_b_buf[kBkLayerBMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_baked_buf[kSdSampleSlots][kSdSampleMaxFrames];
// SDRAM staging buffer for slot 0: SDMMC DMA can write here (D2 SRAM it cannot),
// then SdSampleLoadCommit() copies into the RAM_D2 playback buffer with the CPU.
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_d2_stage[kSdPlaybackD2MaxFrames];

int16_t* SdSampleBuffer(uint8_t slot)
{
    // Slot 0: baked playback lives in RAM_D2 (g_sd_sample_buf0).
    // Slot 1: baked playback lives in SDRAM g_sd_baked_buf[1] (was unused; revived
    // for the bake-at-load path so slot 1 also benefits from the seam-CPU win).
    return (slot == 0u) ? g_sd_sample_buf0 : g_sd_baked_buf[1];
}

int16_t* SdSampleLoadBuffer(uint8_t slot)
{
    return (slot == 0u) ? g_sd_d2_stage : g_sd_sample_buf1;
}

int16_t* SdSampleRawBuffer(uint8_t slot)
{
    return SdSampleLoadBuffer(slot);
}

int16_t* SdRecordBuffer()
{
    return g_sd_record_buf;
}

int16_t* SdManageBuffer()
{
    return g_sd_manage_buf;
}

int16_t* SdBakePreviewBuffer()
{
    return g_sd_bake_preview_buf;
}

int16_t* SdBkLayerBBuffer()
{
    return g_sd_bk_layer_b_buf;
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
