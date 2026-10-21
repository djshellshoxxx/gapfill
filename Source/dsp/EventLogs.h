#pragma once

// Message-thread logs fed by the audio -> message `fillEvents` queue
// (docs/dev/02-FOUNDATIONS.md F0-4, revision R2). They live in the processor,
// not the editor, so history survives the editor being destroyed and recreated.
// Not thread-safe: message thread only.

#include "EngineEvents.h"

#include <array>

namespace gapfill
{

/** The last 8 fills that started, newest first. */
class RecipeHistory
{
public:
    static constexpr int capacity = 8;

    void push (const FillRecipe& r) noexcept
    {
        for (int i = capacity - 1; i > 0; --i)
            items[(size_t) i] = items[(size_t) i - 1];

        items[0] = r;
        count = count < capacity ? count + 1 : capacity;
    }

    int size() const noexcept                       { return count; }

    /** 0 = newest. */
    const FillRecipe& get (int i) const noexcept    { return items[(size_t) juce::jlimit (0, capacity - 1, i)]; }

    void clear() noexcept                           { count = 0; }

private:
    std::array<FillRecipe, (size_t) capacity> items {};
    int count = 0;
};

/** The last 64 gap outcomes (started or rejected), newest first. */
class GapOutcomeLog
{
public:
    static constexpr int capacity = 64;

    void push (const FillEvent& e) noexcept
    {
        write = (write + capacity - 1) % capacity;      // newest at `write`
        items[(size_t) write] = e;
        count = count < capacity ? count + 1 : capacity;
    }

    int size() const noexcept                       { return count; }

    /** 0 = newest. */
    const FillEvent& get (int i) const noexcept
    {
        return items[(size_t) ((write + juce::jlimit (0, capacity - 1, i)) % capacity)];
    }

    void clear() noexcept                           { count = 0; write = 0; }

private:
    std::array<FillEvent, (size_t) capacity> items {};
    int write = 0;
    int count = 0;
};

} // namespace gapfill
