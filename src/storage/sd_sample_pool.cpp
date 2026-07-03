#include "sd_sample_pool.h"

#include "mem_regions.h"

#include <cstring>

// All playback slots live in SDRAM. Per slot we keep a raw load/DMA buffer
// (SDMMC-DMA reachable) and a baked playback buffer; BakeLoopSeamToBuffer renders
// raw -> baked, and the engine reads the baked copy.
ADSR2_SECTION(".sdram_bss") ADSR2_ALIGN32 static int16_t g_sd_sample_raw[kSdSampleSlots][kSdSampleMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_record_buf[kSdSampleMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_manage_buf[kSdManageMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_bake_preview_buf[kSdSampleMaxFrames];
// ~40 MB. Largest single SDRAM allocation in the project, but well within
// budget (64 MB SDRAM total, ~14 MB already used).
// ADSR2_ALIGN32: 32-byte aligned like every other SDMMC-DMA target. The SD
// driver does SCB_Clean/InvalidateDCache_by_Addr on the read destination (32-byte
// granularity); a non-32-aligned base makes those cache ops span into adjacent
// SDRAM and can leave the SDMMC IDMA read failing (FR_DISK_ERR) after heavy prior
// I/O (e.g. a project load). This was the ONLY pool buffer missing the alignment.
ADSR2_SECTION(".sdram_bss") ADSR2_ALIGN32 static int16_t g_sd_bk_layer_b_buf[kBkLayerBMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_baked_buf[kSdSampleSlots][kSdSampleMaxFrames];

static inline uint8_t ClampSlot(uint8_t slot)
{
    return (slot < kSdSampleSlots) ? slot : 0u;
}

int16_t* SdSampleBuffer(uint8_t slot)
{
    // Baked playback buffer (what the engine reads), SDRAM per slot.
    return g_sd_baked_buf[ClampSlot(slot)];
}

int16_t* SdSampleLoadBuffer(uint8_t slot)
{
    return g_sd_sample_raw[ClampSlot(slot)];
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

uint32_t SdManageMaxFrames()
{
    return kSdManageMaxFrames;
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
    return g_sd_baked_buf[ClampSlot(slot)];
}

uint32_t SdSampleMaxFrames(uint8_t slot)
{
    (void)slot; // all slots are SDRAM with the same cap now
    return kSdSampleMaxFrames;
}
