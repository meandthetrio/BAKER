#pragma once

#include <cstddef>
#include <cstdint>

enum class SampleStyle : uint8_t
{
    None = 0,
    Pad,
    Pluck,
    Bass,
    Key,
};

static constexpr uint8_t kSampleStyleOptionCount = 5u;

const char* SampleStyleLabel(SampleStyle style);
char SampleStyleCode(SampleStyle style);
SampleStyle SampleStyleFromCode(char c);
SampleStyle ParseSampleStyleFromFilename(const char* filename);
bool BuildSampleDisplayName(const char* filename, char* out, size_t out_n);
bool BuildStyledSamplePath(const char* existing_path,
                           SampleStyle desired_style,
                           char* out_path,
                           size_t out_path_n);
bool BuildStyledSamplePathFromStem(const char* existing_path,
                                   const char* visible_stem,
                                   SampleStyle desired_style,
                                   char* out_path,
                                   size_t out_path_n);
