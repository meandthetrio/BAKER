#include "sample_style.h"

#include <cassert>
#include <cstring>

namespace
{
void ExpectDisplay(const char* input, const char* expected_name, SampleStyle expected_style)
{
    char display[64];
    const bool ok = BuildSampleDisplayName(input, display, sizeof(display));
    assert(ok);
    assert(std::strcmp(display, expected_name) == 0);
    assert(ParseSampleStyleFromFilename(input) == expected_style);
}

void ExpectStyledPath(const char* existing_path,
                      const char* visible_stem,
                      SampleStyle style,
                      const char* expected_path)
{
    char out[96];
    const bool ok = visible_stem
                        ? BuildStyledSamplePathFromStem(existing_path, visible_stem, style, out, sizeof(out))
                        : BuildStyledSamplePath(existing_path, style, out, sizeof(out));
    assert(ok);
    assert(std::strcmp(out, expected_path) == 0);
}
} // namespace

int main()
{
    assert(std::strcmp(SampleStyleLabel(SampleStyle::None), "----") == 0);
    assert(std::strcmp(SampleStyleLabel(SampleStyle::Hot), "Hot") == 0);
    assert(std::strcmp(SampleStyleLabel(SampleStyle::Dry), "Dry") == 0);
    assert(std::strcmp(SampleStyleLabel(SampleStyle::Wet), "Wet") == 0);
    assert(std::strcmp(SampleStyleLabel(SampleStyle::Cold), "Cold") == 0);

    ExpectDisplay("Kick.wav", "Kick", SampleStyle::None);
    ExpectDisplay("Kick.WAV", "Kick", SampleStyle::None);
    ExpectDisplay("Kick@H.wav", "Kick", SampleStyle::Hot);
    ExpectDisplay("Kick@h.wav", "Kick", SampleStyle::Hot);
    ExpectDisplay("snareRoom@W.wav", "snareRoom", SampleStyle::Wet);
    ExpectDisplay("LoFiHit@D.WAV", "LoFiHit", SampleStyle::Dry);
    ExpectDisplay("PadCold@C.wav", "PadCold", SampleStyle::Cold);
    ExpectDisplay("Sample@Hot.wav", "Sample@Hot", SampleStyle::None);
    ExpectDisplay("A@H@W.wav", "A@H", SampleStyle::Wet);

    ExpectStyledPath("Kick@H.wav", nullptr, SampleStyle::Dry, "Kick@D.wav");
    ExpectStyledPath("Kick@H.wav", nullptr, SampleStyle::None, "Kick.wav");
    ExpectStyledPath("/Samples/Kick@H.wav", nullptr, SampleStyle::Cold, "/Samples/Kick@C.wav");
    ExpectStyledPath("SnareRoom@W.wav", nullptr, SampleStyle::Hot, "SnareRoom@H.wav");
    ExpectStyledPath("Kick@H.wav", "Boom", SampleStyle::Hot, "Boom@H.wav");
    ExpectStyledPath("Pad.wav", "PadSoft", SampleStyle::None, "PadSoft.wav");

    return 0;
}
