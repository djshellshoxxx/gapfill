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
        if (event.type == OutEvent::GapClose)
            for (int i = count - 1; i >= 0; --i)
                if (ev[i].type != OutEvent::GapClose)
                {
                    ev[i] = event;
                    return true;
                }
        return false;
    }
};

using OutEventBuffer = EngineEventBuffer;
}
