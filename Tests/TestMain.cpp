#include "TestFramework.h"
#include "support/TestRig.h"
#include <juce_events/juce_events.h>
#include <iostream>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String filter;
    bool writeGolden = false;

    // --dump-golden <dir>: render golden scenarios A..F and write <dir>/<id>.f32
    // (planar float32) plus fills.txt. Used by the same-runner master comparison
    // (tools/ci_golden_vs_master.sh); touches no checked-in file.
    if (argc >= 3 && juce::String (argv[1]) == "--dump-golden")
    {
        const juce::File dir { juce::String (juce::CharPointer_UTF8 (argv[2])) };
        dir.createDirectory();
        juce::String fills;

        for (const char id : { 'A', 'B', 'C', 'D', 'E', 'F' })
        {
            const auto r = gftest::renderGoldenScenario (id);

            if (! gftest::writeGoldenFile (dir.getChildFile (juce::String::charToString (id) + ".f32"), r.out))
                return 2;

            fills << juce::String::charToString (id) << " " << r.fills << "\n";
        }

        dir.getChildFile ("fills.txt").replaceWithText (fills);
        return 0;
    }

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
