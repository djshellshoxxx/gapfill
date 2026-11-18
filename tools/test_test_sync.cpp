// Standalone regression for Tests/support/TestSync.h (run in CI preflight, also under TSan).
#include "../Tests/support/TestSync.h"

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void require (bool condition, const char* what)
{
    if (! condition)
        throw std::runtime_error (what);
}
}

int main()
{
    using namespace std::chrono_literals;

    // Barrier: 4 workers + owner, two phases per round like the co-op matrix. Every worker
    // must observe the owner's round value written before the first phase, and the owner
    // must observe every worker's write before the second phase completes.
    constexpr int workers = 4, rounds = 100000;
    gftest::TestBarrier phase (workers + 1);
    int round = -1;
    std::array<int, workers> seen {};
    std::atomic<int> mismatches { 0 };
    bool stop = false;
    std::array<std::thread, workers> threads;

    for (int rank = 0; rank < workers; ++rank)
        threads[(std::size_t) rank] = std::thread ([&, rank]
        {
            for (;;)
            {
                phase.arrive_and_wait();
                if (stop)
                    return;
                if (seen[(std::size_t) rank] + 1 != round)
                    mismatches.fetch_add (1);
                seen[(std::size_t) rank] = round;
                phase.arrive_and_wait();
            }
        });

    for (round = 0; round < rounds; ++round)
    {
        // The previous second phase completed, so every worker has written round - 1.
        for (int value : seen)
            if (value != round - 1 && round > 0)
                mismatches.fetch_add (1);
        if (round == 0)
            seen.fill (-1);
        phase.arrive_and_wait();
        phase.arrive_and_wait();
    }

    stop = true;
    phase.arrive_and_wait();
    for (auto& thread : threads)
        thread.join();
    require (mismatches.load() == 0, "barrier phases did not order writes");

    // Semaphore: counting, blocking acquire, and a bounded wait that times out when empty.
    gftest::TestSemaphore semaphore;
    require (! semaphore.try_acquire_for (10ms), "empty semaphore acquired");
    semaphore.release();
    semaphore.release();
    require (semaphore.try_acquire_for (0ms), "first permit missing");
    semaphore.acquire();

    bool delivered = false;
    std::thread producer ([&] { delivered = true; semaphore.release(); });
    semaphore.acquire();
    producer.join();
    require (delivered, "release did not publish the producer's write");

    std::cout << "TestSync: " << rounds << " barrier rounds, semaphore ok" << std::endl;
    return 0;
}
