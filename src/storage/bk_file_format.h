#pragma once

#include <cstdint>

// On-disk format for ".bk" (Baker multisample) files.
//
// Layout:
//   [BkFileHeader (88 bytes, packed, little-endian)] [PCM blob]
//
// PCM blob = (hi_note - lo_note + 1) contiguous slices, each
// `source_duration_samples` int16 LE frames. Slice i (for MIDI note
// lo_note + i) starts at offset i * source_duration_samples * sizeof(int16_t)
// within the blob. Fixed-length slices in v1; variable-length / loop-points
// will use `reserved1` bytes to advertise a per-slice index table when added.
//
// STM32 is little-endian, so reads/writes are direct struct memcpy / f_read /
// f_write. If we ever target a BE host (PC tooling), serialize field-by-field.

namespace bk {

static constexpr uint8_t  kMagic[4]   = { 'B', 'A', 'K', 'E' };
static constexpr uint16_t kVersion    = 1u;
static constexpr uint16_t kHeaderSize = 88u; // sizeof(BkFileHeader); enforced static_assert below.

// Standard MIDI range covered by all .bk files (C1 = 24, C8 = 108).
static constexpr uint8_t kLoNote    = 24u;
static constexpr uint8_t kHiNote    = 108u;
static constexpr uint8_t kSliceCount = static_cast<uint8_t>(kHiNote - kLoNote + 1u); // 85

// algorithm_id tags the producer so readers / future tooling can tell what
// kind of audio is in the slices. 0/1 are testing-only; real pitched bakes
// use kAlgorithmPsola starting in stage 2.
enum BkAlgorithmId : uint8_t
{
    kAlgorithmSilence = 0u,  // stage 1 format-verification bake (all zeros)
    kAlgorithmCopy    = 1u,  // stage 1.5 / early stage 2 (source copied unchanged)
    kAlgorithmPsola   = 2u,  // stage 2+ real pitched bake via Signalsmith-Stretch
};

#pragma pack(push, 1)
struct BkFileHeader
{
    uint8_t  magic[4];                  // 'B','A','K','E'
    uint16_t version;                   // = kVersion
    uint16_t reserved0;                 // = 0, alignment
    uint32_t sample_rate;               // = 48000
    uint16_t channels;                  // = 1
    uint16_t bits_per_sample;           // = 16
    uint32_t source_duration_samples;   // frames per slice (all slices same length in v1)
    uint8_t  root_midi_note;            // MIDI note where the unprocessed source landed
    uint8_t  lo_note;                   // = kLoNote
    uint8_t  hi_note;                   // = kHiNote
    uint8_t  algorithm_id;              // BkAlgorithmId
    char     source_name[32];           // basename of source WAV, null-padded (NOT required null-terminated)
    uint32_t reserved1[8];              // = 0; future flags / per-slice index offset / CRC
};
#pragma pack(pop)

static_assert(sizeof(BkFileHeader) == 88u, "BkFileHeader must be 88 bytes packed");

// Byte offset, into the PCM blob, of slice for `midi_note`. Caller must
// already have validated `lo_note <= midi_note <= hi_note`.
inline uint32_t SliceByteOffset(const BkFileHeader& hdr, uint8_t midi_note)
{
    const uint32_t index = static_cast<uint32_t>(midi_note - hdr.lo_note);
    return index * hdr.source_duration_samples * sizeof(int16_t);
}

// Total PCM-blob byte size (excludes header).
inline uint32_t PcmBlobBytes(const BkFileHeader& hdr)
{
    const uint32_t slice_count = static_cast<uint32_t>(hdr.hi_note - hdr.lo_note + 1u);
    return slice_count * hdr.source_duration_samples * sizeof(int16_t);
}

// Total file size on disk.
inline uint32_t TotalFileBytes(const BkFileHeader& hdr)
{
    return sizeof(BkFileHeader) + PcmBlobBytes(hdr);
}

} // namespace bk
