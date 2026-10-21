#pragma once

// Wait-free single-producer / single-consumer queue (docs/dev/02-FOUNDATIONS.md F0-4).
// Same pattern as VisualisationFifo: preallocated storage behind
// juce::AbstractFifo, so push and pop never block, lock or allocate.

#include <juce_core/juce_core.h>

#include <array>
#include <type_traits>

namespace gapfill
{

/** Exactly one thread may call push(), exactly one (other) thread may call pop().
    Holds up to N items. */
template <typename T, int N>
class SpscQueue
{
    static_assert (std::is_trivially_copyable_v<T>, "queue payloads are copied byte-wise across threads");
    static_assert (N > 0, "queue needs capacity");

public:
    static constexpr int capacity = N;

    /** Producer. Returns false (and drops `item`) when the queue is full. */
    bool push (const T& item) noexcept
    {
        const auto scope = fifo.write (1);

        if (scope.blockSize1 > 0)
            data[(size_t) scope.startIndex1] = item;
        else if (scope.blockSize2 > 0)
            data[(size_t) scope.startIndex2] = item;
        else
            return false;

        return true;
    }

    /** Consumer. Copies up to `maxItems` items into `dest`; returns the count. */
    int pop (T* dest, int maxItems) noexcept
    {
        const auto numReady = juce::jmin (maxItems, fifo.getNumReady());

        if (numReady <= 0)
            return 0;

        const auto scope = fifo.read (numReady);

        for (int i = 0; i < scope.blockSize1; ++i)
            dest[i] = data[(size_t) (scope.startIndex1 + i)];

        for (int i = 0; i < scope.blockSize2; ++i)
            dest[scope.blockSize1 + i] = data[(size_t) (scope.startIndex2 + i)];

        return scope.blockSize1 + scope.blockSize2;
    }

    /** Consumer. Items waiting. */
    int getNumReady() const noexcept   { return fifo.getNumReady(); }

    /** Only while neither thread is using the queue (prepare/reset). */
    void reset() noexcept              { fifo.reset(); }

private:
    // One spare slot: juce::AbstractFifo keeps one element free to tell full from empty.
    juce::AbstractFifo fifo { N + 1 };
    std::array<T, (size_t) N + 1> data {};
};

} // namespace gapfill
