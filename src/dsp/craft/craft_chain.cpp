#include "craft/craft_chain.h"

#include "mem_regions.h"
#include "arm_math.h"
#include "arm_common_tables.h"

#include <cmath>
#include <cstring>

namespace craft {

// Per-slot STFT working sets for Audio Refresh. ~64 KB each. Placed in fast on-chip
// RAM_D2 (NOT external SDRAM): the live audition runs the whole STFT on the audio
// thread, and the per-hop phases (window, OLA accumulate, the 12 KB outAccum memmove,
// the fifos) hammer this struct sequentially every block. On SDRAM those accesses
// spiked the callback ~122%/hop (glitch); RAM_D2 is on-chip and cacheable, so it
// removes that stall. 3 slots * 64 KB = 192 KB — RAM_D2 (256 KB) has the room, and
// .ram_d2_bss is NOLOAD just like .sdram_bss was (CraftRefresh::Reset memsets
// everything it reads, so no zero-init is required). One per slot so two Refresh
// instances in a chain do not alias; the two CraftChain instances (worker render,
// audio audition) are mutually exclusive so sharing the backing is safe.
ADSR2_SECTION(".ram_d2_bss") static RefreshState g_refresh_state[kCraftSlotCount];

// FFT in-place buffers in fast zero-wait DTCM (the strided radix-2 access spiked the
// callback to ~5 ms/frame on external SDRAM). A SINGLE shared 16 KB pair, not per-slot:
// only ONE fresh frame is ever computing at a time (the live preview uses one slot;
// the worker render and audition chains are mutually exclusive). Keeping it to 16 KB
// leaves DTCM ample stack headroom (the main stack lives in DTCM; 48 KB here squeezed
// it). The pointers are applied to the SDRAM state in Reset(), NOT here — writing the
// SDRAM-resident RefreshState from this pre-main constructor faults (SDRAM isn't up).
// re/im are touched only by the SEQUENTIAL spectral stage + pack (cache-friendly), so
// they live in RAM_D2 — this keeps DTCM free for what actually needs zero-wait. The
// real-FFT scratch (cbuf, transform runs in its DTCM half) AND the CMSIS twiddle/bit-rev
// tables go in DTCM: the stock tables sit in QSPI flash and BLOW the 16 KB D-cache, so
// the FFT hit slow QSPI every frame. DTCM is uncached zero-wait — no thrash. The real
// FFT needs the len-1024 internal cfft tables (twiddle + bit-rev) plus the real-merge
// twiddle (twiddleCoef_rfft_2048) — all copied to DTCM. Shared single buffers (one fresh
// frame at a time). Note: the rfft tables total LESS DTCM than the old len-2048 cfft.
ADSR2_SECTION(".ram_d2_bss")  ADSR2_ALIGN32 static float    g_fft_re[kFftSize];
ADSR2_SECTION(".ram_d2_bss")  ADSR2_ALIGN32 static float    g_fft_im[kFftSize];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 static float    g_fft_cbuf[2 * kFftSize];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 static float    g_rfft_cfft_tw[kFftSize];   // len-1024 cfft twiddle (2*1024)
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 static uint16_t g_rfft_cfft_brev[ARMBITREVINDEXTABLE_1024_TABLE_LENGTH];
ADSR2_SECTION(".dtcmram_bss") ADSR2_ALIGN32 static float    g_rfft_tw[kFftSize];         // real-merge twiddle (rfft_2048)
static arm_rfft_fast_instance_f32 g_rfft_dtcm; // real-FFT instance over the DTCM tables

// Shared sqrt-Hann analysis/synthesis window, built ONCE (off the audio thread, at
// first construction) so engaging fresh never pays the 2048-point software-cos build
// inside Reset()->BuildTables_ on the audio thread — that overran a block and clicked
// on engage. Sample-rate independent and identical for every instance, so one copy is
// bound into each slot and memcpy'd into its RefreshState. Plain bss (live pre-main).
static float g_craft_win[kFftSize];

CraftChain::CraftChain()
{
    static bool s_cfft_ready = false;
    if(!s_cfft_ready)
    {
        // Copy CMSIS's real-FFT tables out of slow QSPI flash into fast DTCM and build a
        // private rfft instance over them. QSPI (flash consts) and DTCM are both live
        // pre-main and no SDRAM is touched, so this is safe here. The 2048-pt real FFT
        // wraps a len-1024 complex FFT (its twiddle + bit-rev tables) plus a real-merge
        // twiddle (twiddleCoef_rfft_2048).
        std::memcpy(g_rfft_cfft_tw, twiddleCoef_1024, kFftSize * sizeof(float)); // 2*1024
        std::memcpy(g_rfft_cfft_brev, armBitRevIndexTable1024,
                    ARMBITREVINDEXTABLE_1024_TABLE_LENGTH * sizeof(uint16_t));
        std::memcpy(g_rfft_tw, twiddleCoef_rfft_2048, kFftSize * sizeof(float));
        g_rfft_dtcm.Sint.fftLen       = 1024;
        g_rfft_dtcm.Sint.pTwiddle     = g_rfft_cfft_tw;
        g_rfft_dtcm.Sint.pBitRevTable = g_rfft_cfft_brev;
        g_rfft_dtcm.Sint.bitRevLength = ARMBITREVINDEXTABLE_1024_TABLE_LENGTH;
        g_rfft_dtcm.fftLenRFFT        = 2048;
        g_rfft_dtcm.pTwiddleRFFT      = g_rfft_tw;
        // Build the shared sqrt-Hann window once (here, off the audio thread).
        const float twopi = 6.28318530717958647692f;
        for(int i = 0; i < kFftSize; ++i)
        {
            const float hann = 0.5f * (1.0f - std::cos(twopi * i / static_cast<float>(kFftSize)));
            g_craft_win[i]   = std::sqrt(hann);
        }
        s_cfft_ready = true;
    }
    for(uint8_t s = 0; s < kCraftSlotCount; ++s)
    {
        slots_[s].refresh.BindState(&g_refresh_state[s]);
        slots_[s].refresh.BindFftScratch(g_fft_re, g_fft_im, g_fft_cbuf);
        slots_[s].refresh.BindRfftInstance(&g_rfft_dtcm);
        slots_[s].refresh.BindWinSource(g_craft_win);
    }
}

void CraftChain::ApplyConfig(const CraftChainConfig& cfg, float sample_rate)
{
    cfg_         = cfg;
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;

    for(uint8_t s = 0; s < kCraftSlotCount; ++s)
    {
        const CraftSlotConfig& sc = cfg_.slots[s];
        switch(sc.plugin)
        {
            case kCraftPluginCopy:
                slots_[s].copy.Reset(sample_rate_);
                slots_[s].copy.SetParams(sc.param, sample_rate_);
                break;
            case kCraftPluginDial:
                slots_[s].dial.Reset(sample_rate_);
                slots_[s].dial.SetParams(sc.param, sample_rate_);
                break;
            case kCraftPluginSnap:
                slots_[s].snap.Reset(sample_rate_);
                slots_[s].snap.SetParams(sc.param, sample_rate_);
                break;
            case kCraftPluginWarm:
                slots_[s].warm.Reset(sample_rate_);
                slots_[s].warm.SetParams(sc.param, sample_rate_);
                break;
            case kCraftPluginWarp:
                slots_[s].warp.Reset(sample_rate_);
                slots_[s].warp.SetParams(sc.param, sample_rate_);
                break;
            case kCraftPluginHowl:
                slots_[s].howl.Reset(sample_rate_);
                slots_[s].howl.SetParams(sc.param, sample_rate_);
                break;
            case kCraftPluginFresh:
                slots_[s].refresh.Reset(sample_rate_);
                slots_[s].refresh.SetParams(sc.param, sample_rate_);
                break;
            default: break; // None / not-yet-implemented: nothing to init
        }
    }
}

void CraftChain::UpdateParams(const CraftChainConfig& cfg)
{
    for(uint8_t s = 0; s < kCraftSlotCount; ++s)
    {
        const bool plugin_changed = (cfg.slots[s].plugin != cfg_.slots[s].plugin);
        cfg_.slots[s]             = cfg.slots[s];
        switch(cfg_.slots[s].plugin)
        {
            case kCraftPluginCopy:
                if(plugin_changed)
                    slots_[s].copy.Reset(sample_rate_);
                slots_[s].copy.SetParams(cfg_.slots[s].param, sample_rate_);
                break;
            case kCraftPluginDial:
                if(plugin_changed)
                    slots_[s].dial.Reset(sample_rate_);
                slots_[s].dial.SetParams(cfg_.slots[s].param, sample_rate_);
                break;
            case kCraftPluginSnap:
                if(plugin_changed)
                    slots_[s].snap.Reset(sample_rate_);
                slots_[s].snap.SetParams(cfg_.slots[s].param, sample_rate_);
                break;
            case kCraftPluginWarm:
                if(plugin_changed)
                    slots_[s].warm.Reset(sample_rate_);
                slots_[s].warm.SetParams(cfg_.slots[s].param, sample_rate_);
                break;
            case kCraftPluginWarp:
                if(plugin_changed)
                    slots_[s].warp.Reset(sample_rate_);
                slots_[s].warp.SetParams(cfg_.slots[s].param, sample_rate_);
                break;
            case kCraftPluginHowl:
                if(plugin_changed)
                    slots_[s].howl.Reset(sample_rate_);
                slots_[s].howl.SetParams(cfg_.slots[s].param, sample_rate_);
                break;
            case kCraftPluginFresh:
                if(plugin_changed)
                    slots_[s].refresh.Reset(sample_rate_);
                slots_[s].refresh.SetParams(cfg_.slots[s].param, sample_rate_);
                break;
            default: break;
        }
    }
}

void CraftChain::ProcessSlot_(uint8_t slot, float* buf, uint32_t n)
{
    switch(cfg_.slots[slot].plugin)
    {
        case kCraftPluginCopy: slots_[slot].copy.Process(buf, n); break;
        case kCraftPluginDial: slots_[slot].dial.Process(buf, n); break;
        case kCraftPluginSnap: slots_[slot].snap.Process(buf, n); break;
        case kCraftPluginWarm: slots_[slot].warm.Process(buf, n); break;
        case kCraftPluginWarp: slots_[slot].warp.Process(buf, n); break;
        case kCraftPluginHowl: slots_[slot].howl.Process(buf, n); break;
        case kCraftPluginFresh: slots_[slot].refresh.Process(buf, n); break;
        default: break; // None / not-yet-implemented: pass through
    }
}

void CraftChain::Process(float* buf, uint32_t n)
{
    for(uint8_t s = 0; s < kCraftSlotCount; ++s)
        ProcessSlot_(s, buf, n);
}

bool CraftChain::HasActiveEffect() const
{
    for(uint8_t s = 0; s < kCraftSlotCount; ++s)
        if(CraftPluginParamCount(cfg_.slots[s].plugin) > 0u)
            return true;
    return false;
}

uint32_t CraftChain::Latency() const
{
    uint32_t total = 0u;
    for(uint8_t s = 0; s < kCraftSlotCount; ++s)
        if(cfg_.slots[s].plugin == kCraftPluginFresh)
            total += slots_[s].refresh.Latency();
    return total;
}

} // namespace craft
