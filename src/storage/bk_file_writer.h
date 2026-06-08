#pragma once

#include <cstdint>

#include "bk_file_format.h"

namespace bk {

// Per-slice progress callback fired AFTER each slice's bytes hit SD. Lets
// the caller drive a "writing N/total" UI without exposing the writer's
// chunk loop. Pass nullptr to skip progress reporting.
using BkWriteProgressCb = void (*)(uint32_t slices_done, uint32_t slices_total);

// Write a .bk file to `path` (overwrites if exists; unlinks on any failure
// to avoid leaving partial files on SD). Synchronous — call from main thread
// or worker thread, NEVER from the audio interrupt.
//
// `slice_ptrs` must point to `slice_count` int16 PCM pointers, each pointing
// to at least `frames_per_slice` int16 samples. A `nullptr` entry in
// `slice_ptrs` is treated as "write silence for this slice" — used by the
// stage 1 format-verification bake to avoid allocating a zero buffer.
//
// `hdr` is written verbatim; caller is responsible for filling magic /
// version / sample rate / channels / bits / source_duration / lo / hi /
// algorithm_id / root / source_name / reserved fields correctly. Use
// `MakeDefaultHeader()` below for a pre-filled-with-defaults instance.
//
// Returns true on full success (header + every slice written), false on
// any I/O error or precondition failure. On failure the file is unlinked.
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
