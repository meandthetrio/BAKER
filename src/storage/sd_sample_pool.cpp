#include "sd_sample_pool.h"

#include "mem_regions.h"

ADSR2_SECTION(".sdram_bss") static int16_t g_sd_sample_buf[kSdSampleSlots][kSdSampleMaxFrames];
ADSR2_SECTION(".sdram_bss") static int16_t g_sd_record_buf[kSdSampleMaxFrames];

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

uint32_t SdSampleMaxFrames()
{
    return kSdSampleMaxFrames;
}
