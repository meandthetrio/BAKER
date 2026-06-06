#include "sd_sample_pool.h"

#include "mem_regions.h"

ADSR2_SECTION(".sdram_bss") static int16_t g_sd_sample_buf[kSdSampleSlots][kSdSampleMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_record_buf[kSdSampleMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_manage_buf[kSdSampleMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_baked_buf[kSdSampleSlots][kSdSampleMaxFrames];

int16_t* SdSampleBuffer(uint8_t slot)
{
    if(slot >= kSdSampleSlots)
        slot = 0;
    return g_sd_sample_buf[slot];
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

uint32_t SdSampleMaxFrames()
{
    return kSdSampleMaxFrames;
}
