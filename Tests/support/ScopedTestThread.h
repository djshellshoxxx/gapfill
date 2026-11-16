#pragma once

#include <atomic>
#include <thread>
#include <type_traits>
#include <utility>

namespace gftest
{

// Test-only cooperative worker for standard libraries without C++20 jthread.
// Owns its stop flag and joins before that flag is destroyed, including during
// assertion unwinding. Callables must finish or poll their token; blocking waits
// still need their own release mechanism. Tokens must not outlive this owner.
class ScopedTestThread
{
public:
    class StopToken
    {
    public:
        bool stop_requested() const noexcept
        {
            return flag->load (std::memory_order_acquire);
        }

    private:
        friend class ScopedTestThread;
        explicit StopToken (const std::atomic<bool>& value) noexcept : flag (&value) {}
        const std::atomic<bool>* flag;
    };

    template <typename Fn>
    explicit ScopedTestThread (Fn&& fn)
        : worker ([this, task = std::forward<Fn> (fn)] () mutable
          {
              if constexpr (std::is_invocable_v<decltype(task)&, StopToken>)
                  task (StopToken { stopped });
              else
                  task();
          })
    {}

    ~ScopedTestThread()
    {
        request_stop();
        if (worker.joinable())
            worker.join();
    }

    ScopedTestThread (const ScopedTestThread&) = delete;
    ScopedTestThread& operator= (const ScopedTestThread&) = delete;
    ScopedTestThread (ScopedTestThread&&) = delete;
    ScopedTestThread& operator= (ScopedTestThread&&) = delete;

    void request_stop() noexcept { stopped.store (true, std::memory_order_release); }
    void join() { worker.join(); }

private:
    std::atomic<bool> stopped { false };
    std::thread worker;
};

} // namespace gftest
