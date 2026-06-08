#pragma once

#include <cstdint>

namespace bake {

// One-shot pitch-shift of an entire mono int16 @ 48 kHz buffer using
// signalsmith-stretch's PSOLA-style algorithm. Output frame count equals
// input frame count (length-preserving — purely pitch, no time-stretch).
// `semitones` may be negative (down-shift) or positive (up-shift). Magnitude
// up to ~24 is reasonable; further extremes degrade quality.
//
// CONSTRAINTS:
// - Single-threaded, main thread only. NOT safe to call from audio interrupt.
// - Audio callback should be stopped before calling — PSOLA on the M7
//   fallback FFT is heavy and main-loop-blocking.
// - `frames` must be <= bake::kMaxFrames.
// - `source` and `dest` must point into DMA-irrelevant memory (we don't DMA
//   PSOLA buffers; this is a CPU-only path). Anything addressable works.
//
// FIRST-CALL BEHAVIOR: the SignalsmithStretch instance is lazily configured
// at the first call. Configuration calls `std::vector::resize` on ~9
// internal buffers — roughly 100-200 KB heap. If heap is exhausted, the
// configuration will trigger an abort (we build with -fno-exceptions, so a
// failed allocation is undefined behavior — typically a hardfault). The
// caller has no way to recover from a heap-too-small failure inside this
// function; the only recovery is "build with a custom SDRAM allocator,"
// which we'll add if it actually happens.
//
// Returns true on success; false if args are invalid (null pointers,
// frames == 0, frames > kMaxFrames, |semitones| > kMaxSemitones).
bool RunPitchShift(const int16_t* source,
                   uint32_t       frames,
                   int            semitones,
                   int16_t*       dest);

// Per-chunk progress callback. Called between internal process() chunks so
// the caller can update a UI label + percent bar + flush the display.
// `chunk_index` runs 0..kPsolaChunks-1; `label` is one of the kPsolaPhaseLabel
// strings (cycles through phases). Caller may map (chunk_index+1) /
// kPsolaChunks to a 0-100% bar.
static constexpr uint8_t kPsolaChunks = 5;
extern const char* const kPsolaPhaseLabel[kPsolaChunks];

using PsolaProgressCb = void (*)(uint8_t chunk_index, const char* label);

// Chunked variant: splits the PSOLA process() call into `kPsolaChunks`
// sub-calls. Calls `cb(chunk_index, label)` BEFORE each chunk (i.e. when
// chunk_index == 0 the bar should read 0%, before any work starts; when
// chunk_index == kPsolaChunks-1 the bar reads (4/5)*100=80%). After return,
// the bar should be set to 100% by the caller.
//
// All other constraints and the heap-risk warning of RunPitchShift apply.
bool RunPitchShiftChunked(const int16_t*  source,
                          uint32_t        frames,
                          int             semitones,
                          int16_t*        dest,
                          PsolaProgressCb cb);

// Max source length we can pitch-shift in one call. Matches the existing
// bake-preview SDRAM buffer (240000 frames = 5 s @ 48 kHz mono).
static constexpr uint32_t kMaxFrames     = 240000u;
static constexpr int      kMaxSemitones  = 48;

// Test-bake pitch range relative to the picked root note. Asymmetric (24
// down vs 23 up) keeps the total at a round 47 — see
// BakeMenu_RunPsolaTestBake_ in ui_screen_main.cpp.
static constexpr int kBakeSemitonesDown = 24;
static constexpr int kBakeSemitonesUp   = 23;
static constexpr int kBakeTotalPsola    = kBakeSemitonesDown + kBakeSemitonesUp;

} // namespace bake
