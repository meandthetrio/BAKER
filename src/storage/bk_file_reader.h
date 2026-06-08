#pragma once

#include <cstdint>

#include "bk_file_format.h"

namespace bk {

// Open `path`, read the header, validate magic + version + sample rate +
// channels + bits + lo<=hi + non-zero source_duration, close. Synchronous;
// safe to call from main thread / worker thread, NEVER from audio thread.
//
// Returns true on success; on success, `out_hdr` is fully populated.
// Returns false on any I/O or validation failure; `out_hdr` is left
// untouched (or zeroed at caller's discretion — this function does not write
// to it on failure).
bool BkRead_OpenAndValidateHeader(const char* path, BkFileHeader& out_hdr);

// As above, plus: load the PCM blob (all slices, contiguous) into
// `dst_buf`. `dst_max_frames` is the size of `dst_buf` in int16 samples
// (NOT bytes). Returns false if the file's PCM blob would not fit.
// Synchronous; main/worker only.
bool BkRead_LoadIntoBuffer(const char*   path,
                           int16_t*      dst_buf,
                           uint32_t      dst_max_frames,
                           BkFileHeader& out_hdr);

// Inline: given a parsed header + already-loaded PCM buffer + a midi note,
// return the slice's PCM pointer and frame count. Returns false if midi_note
// is outside [lo_note, hi_note]. No I/O; cheap to call from anywhere.
inline bool BkRead_SliceFromBuffer(const BkFileHeader& hdr,
                                   int16_t*            pcm_buf,
                                   uint8_t             midi_note,
                                   int16_t*&           out_pcm,
                                   uint32_t&           out_length_frames)
{
    if(pcm_buf == nullptr)
        return false;
    if(midi_note < hdr.lo_note || midi_note > hdr.hi_note)
        return false;
    const uint32_t index = static_cast<uint32_t>(midi_note - hdr.lo_note);
    out_pcm           = pcm_buf + (index * hdr.source_duration_samples);
    out_length_frames = hdr.source_duration_samples;
    return true;
}

} // namespace bk
