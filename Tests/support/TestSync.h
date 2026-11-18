#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace gftest
{

// Test-only blocking primitives built on std::mutex + std::condition_variable.
//
// Why not std::barrier / std::binary_semaphore: Apple libc++ implements them on
// its atomic-wait backoff, which stalled the 4-thread co-op matrix (192k phases)
// past the macOS suite deadline, and libstdc++ implements them on raw futexes that
// ThreadSanitizer does not model. Mutex/condvar wake promptly on every platform and
// are understood by every sanitizer. Harness synchronisation only: never use these
// inside processBlock or any code under test.

/** Reusable barrier for a fixed number of participants (std::barrier::arrive_and_wait subset). */
class TestBarrier
{
public:
    explicit TestBarrier (std::ptrdiff_t participants) : expected (participants) {}

    TestBarrier (const TestBarrier&) = delete;
    TestBarrier& operator= (const TestBarrier&) = delete;

    void arrive_and_wait()
    {
        std::unique_lock lock (mutex);
        const auto generation = phase;

        if (++arrived == expected)
        {
            arrived = 0;
            ++phase;
            lock.unlock();
            changed.notify_all();
            return;
        }

        changed.wait (lock, [&] { return phase != generation; });
    }

private:
    std::mutex mutex;
    std::condition_variable changed;
    const std::ptrdiff_t expected;
    std::ptrdiff_t arrived = 0;
    std::size_t phase = 0;
};

/** Counting semaphore (std::counting_semaphore acquire/release/try_acquire_for subset). */
class TestSemaphore
{
public:
    explicit TestSemaphore (std::ptrdiff_t initial = 0) : count (initial) {}

    TestSemaphore (const TestSemaphore&) = delete;
    TestSemaphore& operator= (const TestSemaphore&) = delete;

    void release()
    {
        {
            std::lock_guard lock (mutex);
            ++count;
        }
        available.notify_one();
    }

    void acquire()
    {
        std::unique_lock lock (mutex);
        available.wait (lock, [&] { return count > 0; });
        --count;
    }

    template <typename Rep, typename Period>
    bool try_acquire_for (const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock lock (mutex);

        if (! available.wait_for (lock, timeout, [&] { return count > 0; }))
            return false;

        --count;
        return true;
    }

private:
    std::mutex mutex;
    std::condition_variable available;
    std::ptrdiff_t count;
};

} // namespace gftest
