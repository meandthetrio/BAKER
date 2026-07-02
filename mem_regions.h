#pragma once
//
// Central place for memory placement attributes.
//
// STM32H7 has multiple RAM regions:
// - DTCM: very fast, NOT DMA-safe
// - SRAM/AXI SRAM: general use
// - DMA-safe sections: for buffers touched by peripherals (audio codec, etc.)
// - SDRAM (external): large buffers (delay lines, sample pools)
//
// Step 2: we only define placeholders (no special sections yet) so nothing breaks.
// We’ll “turn these on” in later steps once we choose where big buffers live.
//

// Fast CPU-only memory (not DMA safe)
#define ADSR_FAST_BSS
#define ADSR_FAST_DATA

// DMA-safe buffers (peripheral-friendly)
#define ADSR_DMA_BSS
#define ADSR_DMA_DATA

// External SDRAM (large buffers)
#define ADSR_SDRAM_BSS
#define ADSR_SDRAM_DATA

#define ADSR2_SECTION(name) __attribute__((section(name)))
#define ADSR2_SRAM ADSR2_SECTION(".sram")
#define ADSR2_ALIGN32 __attribute__((aligned(32)))

// Place a function in ITCM (fast, zero-wait, never cache-misses). The linker
// puts .itcm_text in ITCMRAM with a QSPI load copy; main() copies it at boot.
// Gated by ADSR2_ITCM_ENABLED so the placement is a build-time A/B toggle; when
// off it expands to nothing and the function stays in QSPI .text. No `noinline`
// on purpose — within the TU, inlining a tagged helper into another ITCM
// function keeps it in ITCM, and we don't want to block beneficial inlining.
#include "build_config.h"
#if ADSR2_ITCM_ENABLED
#define ADSR2_ITCM_TEXT ADSR2_SECTION(".itcm_text")
#else
#define ADSR2_ITCM_TEXT
#endif
