#include "TestFramework.h"
#include "support/TestRig.h"
#include <juce_events/juce_events.h>
#include <iostream>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String filter;
    bool writeGolden = false;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg == "--write-golden")
            writeGolden = true;
        else
            filter = arg;
    }

    std::cout << "GapFill test suite" << std::endl;

    if (writeGolden)
    {
        // Rewrites Tests/golden/<platform>/*.f32 (and any missing state
        // fixtures). Only done deliberately: see tools/dev/write_golden.sh.
        gftest::writeGoldenMode() = true;
        int failures = gftest::Registry::get().run ("Golden");
        failures += gftest::Registry::get().run ("StateCompat");
        return failures == 0 ? 0 : 1;
    }

    const auto failures = gftest::Registry::get().run (filter);
    return failures == 0 ? 0 : 1;
}
