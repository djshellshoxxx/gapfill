#pragma once

// Dedicated child-process driver: the application's main thread runs JUCE's public
// event loop while a test worker coordinates host operations via callSync(). A fresh
// process prevents stopDispatchLoop() from poisoning later tests in the normal suite.

#include <juce_events/juce_events.h>

#include <iostream>
#include <thread>

namespace gftest
{

inline bool& messageLoopTestMode()
{
    static bool enabled = false;
    return enabled;
}

template <typename RunTests>
int runWithMessageLoop (RunTests&& runTests)
{
    messageLoopTestMode() = true;
    auto* manager = juce::MessageManager::getInstance();
    int result = 1;

    std::jthread worker ([&]
    {
        try
        {
            result = runTests();
        }
        catch (const std::exception& error)
        {
            std::cerr << "Message-loop test worker: " << error.what() << std::endl;
            result = 1;
        }
        catch (...)
        {
            std::cerr << "Message-loop test worker: unknown exception" << std::endl;
            result = 1;
        }

        if (! juce::MessageManager::callAsync ([manager] { manager->stopDispatchLoop(); }))
        {
            std::cerr << "Could not post message-loop shutdown" << std::endl;
            result = 1;
            manager->stopDispatchLoop();
        }
    });

    manager->runDispatchLoop();
    worker.join();
    return result;
}

} // namespace gftest
