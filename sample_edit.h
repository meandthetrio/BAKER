#pragma once

#include <cstdint>

struct SampleEdit
{
    uint32_t start_frame = 0;
    uint32_t end_frame = 0;
    uint32_t loop_start = 0;
    uint32_t loop_end = 0;
    uint8_t  loop_enable = 0;
    float    gain = 1.0f;
};

static inline SampleEdit SampleEdit_Default(uint32_t frames)
{
    SampleEdit e{};
    e.start_frame = 0;
    e.end_frame = frames;
    e.loop_start = 0;
    e.loop_end = frames;
    e.loop_enable = 0;
    e.gain = 1.0f;
    return e;
}

static inline void SampleEdit_Clamp(SampleEdit& e, uint32_t frames)
{
    if(frames == 0)
    {
        e.start_frame = 0;
        e.end_frame = 0;
        e.loop_start = 0;
        e.loop_end = 0;
        e.loop_enable = 0;
        e.gain = 1.0f;
        return;
    }

    if(e.end_frame > frames)
        e.end_frame = frames;
    if(e.start_frame >= e.end_frame)
        e.start_frame = (e.end_frame > 0) ? (e.end_frame - 1) : 0;

    if(e.loop_start < e.start_frame)
        e.loop_start = e.start_frame;
    if(e.loop_start >= e.end_frame)
        e.loop_start = (e.end_frame > 0) ? (e.end_frame - 1) : 0;

    if(e.loop_end <= e.loop_start)
        e.loop_end = e.loop_start + 1;
    if(e.loop_end > e.end_frame)
        e.loop_end = e.end_frame;

    if(e.gain < 0.0f)
        e.gain = 0.0f;
}
