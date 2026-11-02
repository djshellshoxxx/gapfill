#pragma once

// Optional, fixed engine -> MIDI event log. Positions are output-domain except the separate early-hit input log.
// Closing a held gap must survive overflow; a closing event replaces a non-closing event when the buffer is full.

#include <cstdint>

namespace gapfill::midiout
{

struct OutEvent
{
    enum Type : std::uint8_t { Hit = 0, GapOpen, GapClose, FillStart, FillEnd, Confidence };
    Type type = Hit;
    std::int64_t pos = 0;
    float value = 0.0f;                  // normalized strength/confidence
    std::int32_t lengthSamples = 0;      // FillStart planned duration
};

struct EngineEventBuffer
{
    static constexpr int capacity = 64;
    OutEvent ev[capacity] {};
    int count = 0, dropped = 0;

    void clear() noexcept { count = dropped = 0; }
    static int priority (OutEvent::Type type) noexcept
    {
        return type == OutEvent::GapClose ? 3 : type == OutEvent::Confidence ? 0 : type == OutEvent::FillEnd ? 1 : 2;
    }
    bool push (const OutEvent& event) noexcept
    {
        if ((int) event.type > (int) OutEvent::Confidence)
            return false;
        if (count < capacity)
        {
            ev[count++] = event;
            return true;
        }
        ++dropped;
        int victim = -1, lowest = priority (event.type);
        for (int i = count - 1; i >= 0; --i)
            if (priority (ev[i].type) < lowest) { victim = i; lowest = priority (ev[i].type); }
        if (victim >= 0)
        {
            for (int i = victim; i + 1 < count; ++i) ev[i] = ev[i + 1];
            ev[count - 1] = event;
            return true;
        }
        return false;
    }
};

using OutEventBuffer = EngineEventBuffer;
}
