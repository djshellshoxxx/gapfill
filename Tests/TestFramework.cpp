#include "TestFramework.h"
#include <iostream>

namespace gftest
{

Registry& Registry::get()
{
    static Registry r;
    return r;
}

void Registry::add (const juce::String& group, const juce::String& name, std::function<void()> fn)
{
    tests.push_back ({ group, name, std::move (fn) });
}

void failTest (const juce::String& message, const char* file, int line)
{
    throw TestFailure (message + "  [" + juce::File (file).getFileName() + ":" + juce::String (line) + "]");
}

int Registry::run (const juce::String& filter)
{
    int passed = 0, failed = 0;
    juce::String lastGroup;

    for (auto& t : tests)
    {
        if (filter.isNotEmpty() && ! (t.group.containsIgnoreCase (filter) || t.name.containsIgnoreCase (filter)))
            continue;

        if (t.group != lastGroup)
        {
            lastGroup = t.group;
            std::cout << "\n== " << t.group << " ==" << std::endl;
        }

        const auto start = juce::Time::getMillisecondCounterHiRes();

        try
        {
            t.fn();
            const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
            std::cout << "  [PASS] " << t.name << "  (" << juce::String (ms, 1) << " ms)" << std::endl;
            ++passed;
        }
        catch (const std::exception& e)
        {
            std::cout << "  [FAIL] " << t.name << "\n         " << e.what() << std::endl;
            ++failed;
        }
        catch (...)
        {
            std::cout << "  [FAIL] " << t.name << "\n         unknown exception" << std::endl;
            ++failed;
        }
    }

    std::cout << "\n--------------------------------------------------\n"
              << passed << " passed, " << failed << " failed\n" << std::endl;

    return failed;
}

} // namespace gftest
