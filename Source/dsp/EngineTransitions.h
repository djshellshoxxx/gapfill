#pragma once

// Gap / fill state changes seen by the engine during ONE process() call, on the
// output timeline. A small fixed log that other features (F-L11 MIDI output,
// F-X06 co-op) read after the engine ran; the engine never reads it back and it
// holds no pointers. Cleared at the start of every process() call. When full,
// further transitions are counted in `dropped` and not stored.

#include <cstdint>

namespace gapfill
{

struct Transition
{
    enum Type : std::uint8_t { GapOpen = 0, GapClose, FillStart, FillEnd };

    Type type = GapOpen;
    std::uint8_t value = 0;            ///< GapOpen: confidence 0..255; FillStart: velocity 1..127
    std::int64_t pos = 0;              ///< output-timeline sample of the decision
    std::int32_t lengthSamples = 0;    ///< FillStart: planned length
};

struct TransitionLog
{
    static constexpr int capacity = 32;

    Transition items[capacity];
    int count = 0;
    int dropped = 0;

    void clear() noexcept   { count = 0; dropped = 0; }

    void push (Transition::Type type, std::int64_t pos, std::uint8_t value = 0, std::int32_t length = 0) noexcept
    {
        if (count >= capacity)
        {
            ++dropped;
            return;
        }

        items[count++] = { type, value, pos, length };
    }
};

} // namespace gapfill
