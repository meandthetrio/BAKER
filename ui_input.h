#pragma once

#include <cstdint>

enum class UiInputType : uint8_t
{
    EncDelta,
    BtnDown,
    BtnUp,
    BtnLong,
    COUNT
};

enum UiInputButtonId : uint8_t
{
    kUiBtnPod1 = 0,
    kUiBtnPod2,
    kUiBtnLShift,
    kUiBtnRShift,
    kUiBtnPodEnc,
    kUiBtnExtEnc,
    kUiBtnCount
};

enum UiInputEncoderId : uint8_t
{
    kUiEncPod = 0,
    kUiEncExt,
    kUiEncCount
};

struct UiInputEvent
{
    UiInputType type;
    uint8_t     id;    // button id or encoder id
    int16_t     value; // encoder delta (+/-) or unused for buttons
    uint32_t    t_ms;  // timestamp for debugging/long-press
};

static constexpr uint32_t kUiInputQueueSize = 64;

struct UiInputQueue
{
    static constexpr uint32_t kCapacity = kUiInputQueueSize;

    UiInputEvent buffer[kCapacity];
    uint32_t     head = 0;
    uint32_t     tail = 0;
    uint32_t     overflows = 0;
    uint32_t     high_water = 0;
};

bool UiInput_Push(UiInputQueue& q, const UiInputEvent& e);
bool UiInput_Pop(UiInputQueue& q, UiInputEvent& out);
uint32_t UiInput_Dropped(const UiInputQueue& q);
uint32_t UiInput_HighWater(const UiInputQueue& q);
