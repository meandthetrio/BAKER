#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

enum class EventType
{
    TestPing,
    NoteOn,
    NoteOff,
    AllNotesOff,
};

struct Event
{
    EventType type;
    uint8_t   note;     // 0..127 (for NoteOn/NoteOff)
    uint8_t   velocity; // 0..127 (for NoteOn)
    uint16_t  _reserved{0};
    uint32_t  value{0}; // misc payload (e.g. TestPing)

    static constexpr Event TestPingEvent(uint32_t v = 1)
    {
        return {EventType::TestPing, 0, 0, 0, v};
    }

    static constexpr Event NoteOnEvent(uint8_t n, uint8_t vel)
    {
        return {EventType::NoteOn, n, vel, 0, 0};
    }

    static constexpr Event NoteOffEvent(uint8_t n)
    {
        return {EventType::NoteOff, n, 0, 0, 0};
    }

    static constexpr Event AllNotesOffEvent()
    {
        return {EventType::AllNotesOff, 0, 0, 0, 0};
    }
};

class EventQueueSPSC
{
  public:
    static constexpr uint32_t kCapacity = 256;

    bool Push(const Event& e) // MAIN LOOP only
    {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t tail = tail_.load(std::memory_order_acquire);

        if((head - tail) >= kCapacity)
        {
            overflows_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        buffer_[head & (kCapacity - 1)] = e;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool Pop(Event& out) // AUDIO CALLBACK only
    {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t head = head_.load(std::memory_order_acquire);

        if(tail == head)
            return false;

        out = buffer_[tail & (kCapacity - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    uint32_t Overflows() const
    {
        return overflows_.load(std::memory_order_relaxed);
    }

  private:
    static_assert((kCapacity & (kCapacity - 1)) == 0, "kCapacity must be power-of-two");

    alignas(4) Event buffer_[kCapacity];
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};
    std::atomic<uint32_t> overflows_{0};
};
