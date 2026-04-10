#include "ui_input.h"

static_assert((UiInputQueue::kCapacity & (UiInputQueue::kCapacity - 1)) == 0,
              "kUiInputQueueSize must be power-of-two");

bool UiInput_Push(UiInputQueue& q, const UiInputEvent& e)
{
    const uint32_t head = q.head;
    const uint32_t tail = q.tail;
    if((head - tail) >= UiInputQueue::kCapacity)
    {
        q.overflows++;
        return false;
    }

    const uint32_t next_head = head + 1;
    q.buffer[head & (UiInputQueue::kCapacity - 1)] = e;
    const uint32_t level = next_head - tail;
    if(level > q.high_water)
        q.high_water = level;
    q.head = next_head;
    return true;
}

bool UiInput_Pop(UiInputQueue& q, UiInputEvent& out)
{
    const uint32_t tail = q.tail;
    const uint32_t head = q.head;
    if(tail == head)
        return false;

    out = q.buffer[tail & (UiInputQueue::kCapacity - 1)];
    q.tail = tail + 1;
    return true;
}

uint32_t UiInput_Dropped(const UiInputQueue& q)
{
    return q.overflows;
}

uint32_t UiInput_HighWater(const UiInputQueue& q)
{
    return q.high_water;
}
