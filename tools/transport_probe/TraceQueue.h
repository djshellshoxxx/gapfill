#pragma once

// Diagnostic target only. One callback producer, one file-writer consumer.
// All storage exists before processing; no allocation, locks, I/O or retries.
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace gapfill::transportprobe
{
struct Row
{
    std::uint64_t sequence = 0;
    double ppq = 0, bpm = 120, sampleRate = 48000;
    std::int64_t samplePosition = 0;
    std::uint32_t blockSamples = 0;
    bool playing = false, ppqValid = false, tempoValid = false,
         samplePositionValid = false, nonRealtime = false;
};
static_assert (std::is_trivially_copyable_v<Row>);
static_assert (std::atomic<std::uint32_t>::is_always_lock_free
                && std::atomic<std::uint64_t>::is_always_lock_free);

template <std::uint32_t Capacity = 4096>
class Queue
{
    static_assert (Capacity > 1 && (Capacity & (Capacity - 1)) == 0);
public:
    bool push (const Row& value) noexcept
    {
        const auto current = write.load (std::memory_order_relaxed);
        const auto next = (current + 1) & (Capacity - 1);
        if (next == read.load (std::memory_order_acquire))
        {
            dropped.fetch_add (1, std::memory_order_relaxed);
            return false;
        }
        rows[current] = value;
        write.store (next, std::memory_order_release);
        return true;
    }
    bool pop (Row& value) noexcept
    {
        const auto current = read.load (std::memory_order_relaxed);
        if (current == write.load (std::memory_order_acquire)) return false;
        value = rows[current];
        read.store ((current + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }
    std::uint64_t droppedRows() const noexcept { return dropped.load (std::memory_order_relaxed); }
private:
    std::array<Row, Capacity> rows {};
    alignas (64) std::atomic<std::uint32_t> write { 0 };
    alignas (64) std::atomic<std::uint32_t> read { 0 };
    std::atomic<std::uint64_t> dropped { 0 };
};

/** Closing cannot race the last callback push. Only an already-entered callback
    may finish after close(); the consumer waits for Closed before final drain.
    The callback performs bounded CAS operations, never a spin/wait loop. */
class Gate
{
public:
    void open() noexcept { state.store (Open, std::memory_order_release); } // before callbacks only
    bool enter() noexcept
    {
        std::uint32_t expected = Open;
        return state.compare_exchange_strong (expected, Busy, std::memory_order_acquire);
    }
    void leave() noexcept
    {
        std::uint32_t expected = Busy;
        if (! state.compare_exchange_strong (expected, Open, std::memory_order_release))
            state.store (Closed, std::memory_order_release); // the UI/worker changed Busy -> Closing
    }
    void close() noexcept // non-realtime UI/worker/destructor
    {
        auto current = state.load (std::memory_order_acquire);
        while (current == Open || current == Busy)
            if (state.compare_exchange_weak (current, current == Open ? Closed : Closing,
                                              std::memory_order_acq_rel)) return;
    }
    bool closed() const noexcept { return state.load (std::memory_order_acquire) == Closed; }
private:
    enum : std::uint32_t { Closed, Open, Busy, Closing };
    std::atomic<std::uint32_t> state { Closed };
};
}
