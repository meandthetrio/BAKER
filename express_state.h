#pragma once

#include <cstdint>

static constexpr uint8_t kExpressLayerCount = 2u;
static constexpr uint8_t kExpressRowCount = 3u;
static constexpr uint8_t kExpressFocusCount = 1u + (kExpressRowCount * 3u);

enum ExpressTarget : uint8_t
{
    kExpressCutoff = 0,
    kExpressDrive,
    kExpressResonance,
    kExpressAttack,
    kExpressSustain,
    kExpressRelease,
    kExpressReverb,
    kExpressPolyPorto,
    kExpressNone,
    kExpressTargetCount,
};

enum ExpressPolyPortoSourceMode : uint8_t
{
    kExpressPolyPortoSourceClosest = 0u,
    kExpressPolyPortoSourceLatest = 1u,
};

static constexpr uint8_t  kExpressPolyPortoVoicesMin = 2u;
static constexpr uint8_t  kExpressPolyPortoVoicesMax = 10u;
static constexpr uint8_t  kExpressPolyPortoVoicesDefault = 3u;
static constexpr uint16_t kExpressPolyPortoTimeMinMs = 100u;
static constexpr uint16_t kExpressPolyPortoTimeMaxMs = 1000u;
static constexpr uint16_t kExpressPolyPortoTimeDefaultMs = 150u;
static constexpr uint16_t kExpressPolyPortoTimeStepMs = 10u;
static constexpr uint16_t kExpressPolyPortoSlideMinMs = kExpressPolyPortoTimeMinMs;
static constexpr uint16_t kExpressPolyPortoSlideMaxMs = kExpressPolyPortoTimeMaxMs;
static constexpr uint16_t kExpressPolyPortoSlideDefaultMs = kExpressPolyPortoTimeDefaultMs;
static constexpr uint8_t  kExpressPolyPortoRangeMinSemitones = 1u;
static constexpr uint8_t  kExpressPolyPortoRangeMaxSemitones = 7u;
static constexpr uint8_t  kExpressPolyPortoRangeDefaultSemitones = 7u;
static constexpr uint16_t kExpressPolyPortoReleaseMinMs = 100u;
static constexpr uint16_t kExpressPolyPortoReleaseMaxMs = 1000u;
static constexpr uint16_t kExpressPolyPortoReleaseDefaultMs = 200u;
static constexpr uint16_t kExpressPolyPortoReleaseStepMs = 100u;

inline int ExpressClampInt(int value, int lo, int hi)
{
    if(value < lo)
        return lo;
    if(value > hi)
        return hi;
    return value;
}

inline uint8_t ExpressClampTarget(int target)
{
    if(target < 0 || target >= static_cast<int>(kExpressTargetCount))
        return kExpressNone;
    return static_cast<uint8_t>(target);
}

inline bool ExpressTargetIsGlobal(uint8_t target)
{
    return ExpressClampTarget(target) == kExpressReverb;
}

inline bool ExpressTargetIsNone(uint8_t target)
{
    return ExpressClampTarget(target) == kExpressNone;
}

inline bool ExpressTargetIsPolyPorto(uint8_t target)
{
    return ExpressClampTarget(target) == kExpressPolyPorto;
}

inline const char* ExpressTargetLabel(uint8_t target)
{
    switch(ExpressClampTarget(target))
    {
        case kExpressNone: return "none";
        case kExpressCutoff: return "cutoff";
        case kExpressDrive: return "drive";
        case kExpressResonance: return "reso";
        case kExpressAttack: return "attack";
        case kExpressSustain: return "sustain";
        case kExpressRelease: return "release";
        case kExpressReverb: return "reverb";
        case kExpressPolyPorto: return "polyporto";
        default: return "none";
    }
}

inline uint16_t ExpressTargetMin(uint8_t target)
{
    switch(ExpressClampTarget(target))
    {
        case kExpressNone: return 0u;
        case kExpressCutoff: return 20u;
        case kExpressDrive: return 0u;
        case kExpressResonance: return 0u;
        case kExpressAttack: return 2u;
        case kExpressSustain: return 0u;
        case kExpressRelease: return 1u;
        case kExpressReverb: return 0u;
        case kExpressPolyPorto: return 0u;
        default: return 0u;
    }
}

inline uint16_t ExpressTargetMax(uint8_t target)
{
    switch(ExpressClampTarget(target))
    {
        case kExpressNone: return 0u;
        case kExpressCutoff: return 20000u;
        case kExpressDrive: return 60u;
        case kExpressResonance: return 100u;
        case kExpressAttack: return 4000u;
        case kExpressSustain: return 100u;
        case kExpressRelease: return 4000u;
        case kExpressReverb: return 100u;
        case kExpressPolyPorto: return 1u;
        default: return 100u;
    }
}

inline int ExpressTargetStep(uint8_t target)
{
    switch(ExpressClampTarget(target))
    {
        case kExpressNone: return 1;
        case kExpressCutoff: return 100;
        case kExpressDrive: return 1;
        case kExpressAttack: return 5;
        case kExpressRelease: return 5;
        default: return 1;
    }
}

inline void ExpressClampPolyPortoConfig(uint8_t& voice_limit,
                                        uint16_t& slide_ms,
                                        uint8_t& source_range_semitones,
                                        uint8_t& source_mode,
                                        uint16_t& release_ms)
{
    voice_limit = static_cast<uint8_t>(ExpressClampInt(static_cast<int>(voice_limit),
                                                       static_cast<int>(kExpressPolyPortoVoicesMin),
                                                       static_cast<int>(kExpressPolyPortoVoicesMax)));
    slide_ms = static_cast<uint16_t>(ExpressClampInt(static_cast<int>(slide_ms),
                                                     static_cast<int>(kExpressPolyPortoSlideMinMs),
                                                     static_cast<int>(kExpressPolyPortoSlideMaxMs)));
    source_range_semitones
        = static_cast<uint8_t>(ExpressClampInt(static_cast<int>(source_range_semitones),
                                               static_cast<int>(kExpressPolyPortoRangeMinSemitones),
                                               static_cast<int>(kExpressPolyPortoRangeMaxSemitones)));
    source_mode = static_cast<uint8_t>(ExpressClampInt(static_cast<int>(source_mode),
                                                       static_cast<int>(kExpressPolyPortoSourceClosest),
                                                       static_cast<int>(kExpressPolyPortoSourceLatest)));
    release_ms = static_cast<uint16_t>(ExpressClampInt(static_cast<int>(release_ms),
                                                       static_cast<int>(kExpressPolyPortoReleaseMinMs),
                                                       static_cast<int>(kExpressPolyPortoReleaseMaxMs)));
}

inline void ExpressClampRow(uint8_t& target, uint16_t& min_value, uint16_t& max_value)
{
    target = ExpressClampTarget(target);
    const uint16_t lo = ExpressTargetMin(target);
    const uint16_t hi = ExpressTargetMax(target);
    min_value = static_cast<uint16_t>(ExpressClampInt(static_cast<int>(min_value), lo, hi));
    max_value = static_cast<uint16_t>(ExpressClampInt(static_cast<int>(max_value), lo, hi));
    if(min_value > max_value)
    {
        min_value = lo;
        max_value = hi;
    }
}

inline bool ExpressCanAssignTarget(const uint8_t targets[kExpressLayerCount][kExpressRowCount],
                                   uint8_t layer,
                                   uint8_t row,
                                   uint8_t candidate)
{
    const uint8_t safe_layer = layer & 1u;
    const uint8_t safe_row = row % kExpressRowCount;
    const uint8_t safe_target = ExpressClampTarget(candidate);
    if(ExpressTargetIsNone(safe_target))
        return true;

    for(uint8_t other = 0; other < kExpressRowCount; ++other)
    {
        if(other == safe_row)
            continue;
        if(ExpressClampTarget(targets[safe_layer][other]) == safe_target)
            return false;
    }

    if(safe_target != kExpressReverb)
        return true;

    for(uint8_t other_layer = 0; other_layer < kExpressLayerCount; ++other_layer)
    {
        for(uint8_t other_row = 0; other_row < kExpressRowCount; ++other_row)
        {
            if(other_layer == safe_layer && other_row == safe_row)
                continue;
            if(ExpressClampTarget(targets[other_layer][other_row]) == kExpressReverb)
                return false;
        }
    }
    return true;
}

inline uint8_t ExpressAdvanceSelectableTarget(const uint8_t targets[kExpressLayerCount][kExpressRowCount],
                                              uint8_t layer,
                                              uint8_t row,
                                              uint8_t current_target,
                                              int delta)
{
    if(delta == 0)
        return ExpressClampTarget(current_target);

    uint8_t next = ExpressClampTarget(current_target);
    const int dir = (delta > 0) ? 1 : -1;
    int steps = (delta > 0) ? delta : -delta;
    while(steps-- > 0)
    {
        for(uint8_t guard = 0; guard < kExpressTargetCount; ++guard)
        {
            int candidate = static_cast<int>(next) + dir;
            if(candidate < 0)
                candidate = static_cast<int>(kExpressTargetCount) - 1;
            else if(candidate >= static_cast<int>(kExpressTargetCount))
                candidate = 0;
            next = static_cast<uint8_t>(candidate);
            if(ExpressCanAssignTarget(targets, layer, row, next))
                break;
        }
    }
    return next;
}

inline void ExpressNormalizeAssignments(uint8_t targets[kExpressLayerCount][kExpressRowCount],
                                        uint16_t min_value[kExpressLayerCount][kExpressRowCount],
                                        uint16_t max_value[kExpressLayerCount][kExpressRowCount])
{
    bool used_in_layer[kExpressLayerCount][kExpressTargetCount] = {};
    bool reverb_used = false;

    for(uint8_t layer = 0; layer < kExpressLayerCount; ++layer)
    {
        for(uint8_t row = 0; row < kExpressRowCount; ++row)
        {
            uint8_t target = targets[layer][row];
            uint16_t min_v = min_value[layer][row];
            uint16_t max_v = max_value[layer][row];
            ExpressClampRow(target, min_v, max_v);

            if(!ExpressTargetIsNone(target)
               && (used_in_layer[layer][target] || (target == kExpressReverb && reverb_used)))
            {
                for(uint8_t attempt = 0; attempt < kExpressTargetCount; ++attempt)
                {
                    const uint8_t candidate
                        = static_cast<uint8_t>((target + attempt + 1u) % kExpressTargetCount);
                    if(candidate != kExpressNone && used_in_layer[layer][candidate])
                        continue;
                    if(candidate == kExpressReverb && reverb_used)
                        continue;
                    target = candidate;
                    min_v = ExpressTargetMin(target);
                    max_v = ExpressTargetMax(target);
                    break;
                }
            }

            ExpressClampRow(target, min_v, max_v);
            targets[layer][row] = target;
            min_value[layer][row] = min_v;
            max_value[layer][row] = max_v;
            if(!ExpressTargetIsNone(target))
                used_in_layer[layer][target] = true;
            if(target == kExpressReverb)
                reverb_used = true;
        }
    }
}

inline bool ExpressLayerOwnsTarget(const uint8_t targets[kExpressLayerCount][kExpressRowCount],
                                   uint8_t layer,
                                   uint8_t target)
{
    const uint8_t safe_layer = layer & 1u;
    const uint8_t safe_target = ExpressClampTarget(target);
    if(ExpressTargetIsNone(safe_target))
        return false;
    for(uint8_t row = 0; row < kExpressRowCount; ++row)
    {
        if(ExpressClampTarget(targets[safe_layer][row]) == safe_target)
            return true;
    }
    return false;
}

inline bool ExpressAnyLayerOwnsTarget(const uint8_t targets[kExpressLayerCount][kExpressRowCount],
                                      uint8_t target)
{
    const uint8_t safe_target = ExpressClampTarget(target);
    if(ExpressTargetIsNone(safe_target))
        return false;
    for(uint8_t layer = 0; layer < kExpressLayerCount; ++layer)
    {
        if(ExpressLayerOwnsTarget(targets, layer, safe_target))
            return true;
    }
    return false;
}
