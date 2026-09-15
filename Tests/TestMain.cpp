#include "TestFramework.h"
#include <juce_events/juce_events.h>
#include <iostream>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String filter;

    if (argc > 1)
        filter = juce::String (argv[1]);

    std::cout << "GapFill test suite" << std::endl;

    const auto failures = gftest::Registry::get().run (filter);
    return failures == 0 ? 0 : 1;
}
