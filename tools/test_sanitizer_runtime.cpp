// Exercise the timed condition-variable primitive used by JUCE WaitableEvent.
// GCC 11's TSan does not intercept pthread_cond_clockwait, causing false double
// lock reports before project concurrency tests can run (GCC PR 101978).
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

int main()
{
    for (int iteration = 0; iteration < 32; ++iteration)
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool ready = false;
        std::unique_lock<std::mutex> lock (mutex);
        std::thread worker ([&]
        {
            std::lock_guard<std::mutex> guard (mutex);
            ready = true;
            changed.notify_one();
        });
        // The worker cannot acquire the mutex until this timed wait releases it.
        const bool completed = changed.wait_for (lock, std::chrono::seconds (5), [&] { return ready; });
        lock.unlock();
        worker.join();
        if (! completed) return 1;
    }
    std::cout << "PASS: sanitizer runtime timed-wait synchronization\n";
}
