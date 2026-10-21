#pragma once

// Lock-free "latest value wins" hand-off from ONE writer thread (message
// thread) to ONE reader thread (audio thread): the classic triple buffer.
// Neither side ever blocks, waits or allocates; the reader always sees a
// complete, consistent T (never a half-written one) and skips intermediate
// publications. T must be trivially copyable.

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace gapfill
{

template <typename T>
class TripleBuffer
{
    static_assert (std::is_trivially_copyable_v<T>, "published payloads are copied byte-wise");

public:
    TripleBuffer() = default;
    explicit TripleBuffer (const T& initial)
    {
        for (auto& b : buffers)
            b = initial;
    }

    /** Writer thread. Copies `value` in and makes it the newest publication. */
    void publish (const T& value) noexcept
    {
        buffers[(size_t) writeIdx] = value;
        const auto previous = middle.exchange ((unsigned) writeIdx | dirtyBit, std::memory_order_acq_rel);
        writeIdx = (int) (previous & 3u);
    }

    /** Reader thread. Returns true (and updates `out`) only when something new
        was published since the last call. */
    bool consume (T& out) noexcept
    {
        if ((middle.load (std::memory_order_acquire) & dirtyBit) == 0)
            return false;

        const auto previous = middle.exchange ((unsigned) readIdx, std::memory_order_acq_rel);
        readIdx = (int) (previous & 3u);
        out = buffers[(size_t) readIdx];
        return true;
    }

private:
    static constexpr unsigned dirtyBit = 4u;

    std::array<T, 3> buffers {};
    std::atomic<unsigned> middle { 1u };   // index of the middle buffer (+ dirty flag)
    int writeIdx = 0;                      // owned by the writer
    int readIdx = 2;                       // owned by the reader
};

} // namespace gapfill
