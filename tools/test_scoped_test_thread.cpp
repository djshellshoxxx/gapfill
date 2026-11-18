#include "../Tests/support/ScopedTestThread.h"

#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
void require (bool condition)
{
    if (! condition)
        throw std::runtime_error ("ScopedTestThread regression");
}
}

int main()
{
    // Destruction joins finite work, including a move-only callable.
    int result = 0;
    {
        gftest::ScopedTestThread worker ([&, value = std::make_unique<int> (42)]
        {
            result = *value;
        });
    }
    require (result == 42);

    // Explicit cancellation/join publishes the worker's writes and must not
    // cause a second join when the owner subsequently leaves scope.
    bool stopped = false;
    {
        gftest::ScopedTestThread worker ([&] (auto token)
        {
            while (! token.stop_requested())
                std::this_thread::yield();
            stopped = true;
        });
        worker.request_stop();
        worker.request_stop();
        worker.join();
        require (stopped);
    }

    // A failed test assertion unwinds both concurrent workers before captured
    // fixture state is destroyed; each worker has its own stop flag.
    bool firstStopped = false, secondStopped = false;
    try
    {
        gftest::ScopedTestThread first ([&] (auto token)
        {
            while (! token.stop_requested())
                std::this_thread::yield();
            firstStopped = true;
        });
        gftest::ScopedTestThread second ([&] (auto token)
        {
            while (! token.stop_requested())
                std::this_thread::yield();
            secondStopped = true;
        });
        throw std::runtime_error ("simulated assertion");
    }
    catch (const std::runtime_error&) {}
    require (firstStopped && secondStopped);
    std::cout << "PASS: scoped test thread completion, cancellation and unwinding\n";
}
