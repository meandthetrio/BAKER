#include "sample_style.h"

#include <cstdio>
#include <cstring>

namespace
{
char ToAsciiUpper(char c)
{
    if(c >= 'a' && c <= 'z')
        return static_cast<char>(c - ('a' - 'A'));
    return c;
}

char ToAsciiLower(char c)
{
    if(c >= 'A' && c <= 'Z')
        return static_cast<char>(c + ('a' - 'A'));
    return c;
}

const char* BasenameStart(const char* path)
{
    if(!path)
        return nullptr;

    const char* base = path;
    for(const char* p = path; *p != '\0'; ++p)
    {
        if(*p == '/' || *p == '\\')
            base = p + 1;
    }
    return base;
}

bool IsWavExtension(const char* ext)
{
    return ext && ext[0] == '.'
           && ToAsciiLower(ext[1]) == 'w'
           && ToAsciiLower(ext[2]) == 'a'
           && ToAsciiLower(ext[3]) == 'v'
           && ext[4] == '\0';
}

const char* FindWavExtension(const char* basename)
{
    if(!basename)
        return nullptr;

    const size_t len = std::strlen(basename);
    if(len < 4u)
        return nullptr;

    const char* ext = basename + (len - 4u);
    return IsWavExtension(ext) ? ext : nullptr;
}

SampleStyle ParseTrailingStyle(const char* basename, const char* ext)
{
    if(!basename || !ext)
        return SampleStyle::None;

    const ptrdiff_t stem_len = ext - basename;
    if(stem_len < 2)
        return SampleStyle::None;
    if(ext[-2] != '@')
        return SampleStyle::None;
    return SampleStyleFromCode(ext[-1]);
}

bool CopySubstring(const char* src, size_t len, char* out, size_t out_n)
{
    if(!out || out_n == 0u)
        return false;

    if(!src)
    {
        out[0] = '\0';
        return false;
    }

    const int written = std::snprintf(out, out_n, "%.*s", static_cast<int>(len), src);
    if(written < 0 || written >= static_cast<int>(out_n))
    {
        out[out_n - 1u] = '\0';
        return false;
    }
    return true;
}

bool CopyStringChecked(const char* src, char* out, size_t out_n)
{
    if(!out || out_n == 0u)
        return false;
    if(!src)
    {
        out[0] = '\0';
        return false;
    }

    const int written = std::snprintf(out, out_n, "%s", src);
    if(written < 0 || written >= static_cast<int>(out_n))
    {
        out[out_n - 1u] = '\0';
        return false;
    }
    return true;
}

bool BuildStyledSamplePathInternal(const char* existing_path,
                                   const char* visible_stem,
                                   SampleStyle desired_style,
                                   char* out_path,
                                   size_t out_path_n)
{
    if(!existing_path || existing_path[0] == '\0' || !visible_stem || !out_path || out_path_n == 0u)
        return false;

    const char* base = BasenameStart(existing_path);
    if(!base)
        return false;

    const size_t dir_len = static_cast<size_t>(base - existing_path);
    const char* ext = FindWavExtension(base);
    const char* ext_text = ext ? ext : ".wav";
    const char style_code = SampleStyleCode(desired_style);

    const int written = (style_code != '\0')
                            ? std::snprintf(out_path,
                                            out_path_n,
                                            "%.*s%s@%c%s",
                                            static_cast<int>(dir_len),
                                            existing_path,
                                            visible_stem,
                                            style_code,
                                            ext_text)
                            : std::snprintf(out_path,
                                            out_path_n,
                                            "%.*s%s%s",
                                            static_cast<int>(dir_len),
                                            existing_path,
                                            visible_stem,
                                            ext_text);
    if(written < 0 || written >= static_cast<int>(out_path_n))
    {
        out_path[out_path_n - 1u] = '\0';
        return false;
    }
    return true;
}
} // namespace

const char* SampleStyleLabel(SampleStyle style)
{
    switch(style)
    {
        case SampleStyle::Pad: return "Pad";
        case SampleStyle::Pluck: return "Pluck";
        case SampleStyle::Bass: return "Bass";
        case SampleStyle::Key: return "Key";
        case SampleStyle::None:
        default: return "----";
    }
}

char SampleStyleCode(SampleStyle style)
{
    switch(style)
    {
        case SampleStyle::Pad: return 'P';
        case SampleStyle::Pluck: return 'L';
        case SampleStyle::Bass: return 'B';
        case SampleStyle::Key: return 'K';
        case SampleStyle::None:
        default: return '\0';
    }
}

SampleStyle SampleStyleFromCode(char c)
{
    switch(ToAsciiUpper(c))
    {
        case 'P': return SampleStyle::Pad;
        case 'L': return SampleStyle::Pluck;
        case 'B': return SampleStyle::Bass;
        case 'K': return SampleStyle::Key;
        default: return SampleStyle::None;
    }
}

SampleStyle ParseSampleStyleFromFilename(const char* filename)
{
    const char* base = BasenameStart(filename);
    const char* ext = FindWavExtension(base);
    return ParseTrailingStyle(base, ext);
}

bool BuildSampleDisplayName(const char* filename, char* out, size_t out_n)
{
    if(!out || out_n == 0u)
        return false;

    out[0] = '\0';
    if(!filename || filename[0] == '\0')
        return false;

    const char* base = BasenameStart(filename);
    if(!base)
        return false;

    const char* ext = FindWavExtension(base);
    if(!ext)
        return CopyStringChecked(base, out, out_n);

    size_t visible_len = static_cast<size_t>(ext - base);
    if(ParseTrailingStyle(base, ext) != SampleStyle::None)
        visible_len -= 2u;

    return CopySubstring(base, visible_len, out, out_n);
}

bool BuildStyledSamplePath(const char* existing_path,
                           SampleStyle desired_style,
                           char* out_path,
                           size_t out_path_n)
{
    char visible_stem[256];
    if(!BuildSampleDisplayName(existing_path, visible_stem, sizeof(visible_stem)))
        return false;
    return BuildStyledSamplePathInternal(existing_path, visible_stem, desired_style, out_path, out_path_n);
}

bool BuildStyledSamplePathFromStem(const char* existing_path,
                                   const char* visible_stem,
                                   SampleStyle desired_style,
                                   char* out_path,
                                   size_t out_path_n)
{
    return BuildStyledSamplePathInternal(existing_path,
                                         visible_stem,
                                         desired_style,
                                         out_path,
                                         out_path_n);
}
