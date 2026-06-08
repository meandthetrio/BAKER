#pragma once

#include <cstdint>

#include "bk_file_format.h"

namespace bk {

// Per-slice progress callback fired AFTER each slice's bytes hit SD. Lets
// the caller drive a "writing N/total" UI without exposing the writer's
// chunk loop. Pass nullptr to skip progress reporting.
using BkWriteProgressCb = void (*)(uint32_t slices_done, uint32_t slices_total);

// ---- Streaming writer (new in stage 3) -----------------------------------
//
// The streaming API lets the bake driver compute one PSOLA slice at a time
// into a single scratch buffer and ship each slice straight to SD, instead
// of holding all 47 in RAM. Slots are written in forward order (0 → 84);
// BkWriter_End pads any unwritten slots with silence so the file is always
// the expected 85-slot length.
//
// All gotchas from the one-shot writer still apply: the FIL struct and any
// header / data buffer handed to f_write must live in DMA-reachable memory
// (SDRAM or AXI-SRAM, NOT DTCM or RAM_D2). The writer owns its own SDRAM
// FIL, header copy buffer, and zero chunk internally — callers only need to
// ensure THEIR slice data is DMA-reachable when they call WriteSlice.
struct BkWriter
{
    uint32_t frames_per_slice;
    uint32_t slice_bytes;
    uint32_t slices_total;   // = kSliceCount once Begin succeeds
    uint32_t slot_cursor;    // next slot index to emit (0..slices_total)
    bool     open;
};

// Open `path` (overwriting any existing file), write the header. Returns
// false on any failure; on failure no file is left behind.
bool BkWriter_Begin(BkWriter&           w,
                    const char*         path,
                    const BkFileHeader& hdr,
                    uint32_t            frames_per_slice);

// Emit one slice at slot_cursor and advance. `src == nullptr` writes a
// silence slice using the internal zero chunk. Returns false on I/O error
// (caller should still call End() with the same path so the partial file
// is unlinked).
bool BkWriter_WriteSlice(BkWriter& w, const int16_t* src);

// Pad any remaining unwritten slots with silence, close the file. If
// anything failed along the way (writer not open, write error, close
// error) the file at `path` is unlinked. Returns true only on full
// success.
//
// `cb`, if non-null, fires after each silence-pad slice hits SD with
// (slots_padded_done, slots_padded_total) — lets the caller drive a
// "finalizing X/N" progress bar during the silence-fill phase, which
// for typical bakes (47 PSOLA + 1 root) is 37 slots of zero writes and
// can take 1–2 s on SD for multi-second source files.
bool BkWriter_End(BkWriter& w, const char* path, BkWriteProgressCb cb = nullptr);

// ---- One-shot wrapper ----------------------------------------------------
//
// Equivalent to Begin + N×WriteSlice + End. Kept for callsites that already
// have all slice pointers in hand and don't need streaming.
bool BkWrite_File(const char*            path,
                  const BkFileHeader&    hdr,
                  const int16_t* const*  slice_ptrs,
                  uint32_t               slice_count,
                  uint32_t               frames_per_slice,
                  BkWriteProgressCb      cb = nullptr);

// Convenience: populate a BkFileHeader with sensible defaults
// (magic + version + 48 kHz mono 16-bit + lo/hi from constants). Caller fills
// in `source_duration_samples`, `root_midi_note`, `algorithm_id`, and
// `source_name`.
BkFileHeader MakeDefaultHeader();

} // namespace bk
