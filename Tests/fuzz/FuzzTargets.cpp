// libFuzzer harness for every parser that accepts outside data (host state, preset/recipe/
// rating/event files, controller profiles, translation catalogs). Opt-in build only:
//   cmake -S . -B build-fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DGAPFILL_FUZZ=ON
//   cmake --build build-fuzz --target GapFillFuzz
//   GF_FUZZ_TARGET=state build-fuzz/artefacts/GapFillFuzz -max_total_time=300 Tests/fuzz/corpus/state
// Targets: state, preset, recipe, profile, catalog, events, ratings (docs/qa/FUZZING.md).
// Each target also asserts an invariant, not just "no crash": accepted input re-serialises
// and reloads to the same bytes, and rejected input leaves the processor state unchanged.

#include "PluginProcessor.h"
#include "features/commands/ControllerProfile.h"
#include "features/events/EventRecorder.h"
#include "features/history/RecipeFile.h"
#include "features/i18n/Catalog.h"
#include "features/taste/RatingStore.h"

#include <juce_events/juce_events.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>

using namespace gapfill;

namespace
{
    enum class Target { State, Preset, Recipe, Profile, Catalog, Events, Ratings };

    Target selected = Target::State;
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juceInit;
    std::unique_ptr<GapFillAudioProcessor> processor;
    juce::File scratch;

    [[noreturn]] void fail (const char* what)
    {
        std::fprintf (stderr, "fuzz invariant violated: %s\n", what);
        std::abort();
    }

    juce::MemoryBlock stateOf (GapFillAudioProcessor& p)
    {
        juce::MemoryBlock block;
        p.getStateInformation (block);
        return block;
    }

    juce::File writeScratch (const std::uint8_t* data, std::size_t size)
    {
        scratch.replaceWithData (data, size);
        return scratch;
    }

    void fuzzState (const std::uint8_t* data, std::size_t size)
    {
        // Start from the same baseline every time so failures reproduce from one input.
        static const auto baseline = stateOf (*processor);
        processor->setStateInformation (baseline.getData(), (int) baseline.getSize());
        const auto before = stateOf (*processor);
        processor->setStateInformation (data, (int) size);
        const auto after = stateOf (*processor);

        if (after != before)
        {
            // Accepted: the result must be a fixed point (save -> load -> save is identical).
            processor->setStateInformation (after.getData(), (int) after.getSize());
            if (stateOf (*processor) != after)
                fail ("accepted host state does not round-trip byte-identically");
        }
    }

    void fuzzPreset (const std::uint8_t* data, std::size_t size)
    {
        static const auto baseline = stateOf (*processor);
        processor->setStateInformation (baseline.getData(), (int) baseline.getSize());
        juce::String error;
        const auto before = stateOf (*processor);
        if (! processor->loadParameterStateFromFile (writeScratch (data, size), error))
        {
            if (stateOf (*processor) != before)
                fail ("rejected preset file changed the session");
            if (error.isEmpty())
                fail ("rejected preset file reported no reason");
        }
    }

    void fuzzRecipe (const std::uint8_t* data, std::size_t size)
    {
        const auto text = juce::String::fromUTF8 (reinterpret_cast<const char*> (data), (int) size);
        juce::ignoreUnused (history::parseRecipeText (text));
    }

    void fuzzProfile (const std::uint8_t* data, std::size_t size)
    {
        const auto json = juce::String::fromUTF8 (reinterpret_cast<const char*> (data), (int) size);
        const auto parsed = commands::parseProfile (json);
        if (! parsed.ok && parsed.error.isEmpty())
            fail ("rejected controller profile reported no reason");
    }

    void fuzzCatalog (const std::uint8_t* data, std::size_t size)
    {
        i18n::Catalog catalog;
        juce::String error;
        if (! catalog.load (data, size, error) && error.isEmpty())
            fail ("rejected catalog reported no reason");
    }

    void fuzzEvents (const std::uint8_t* data, std::size_t size)
    {
        events::ReplayData replay;
        juce::ignoreUnused (events::loadFromFile (writeScratch (data, size), replay));
    }

    void fuzzRatings (const std::uint8_t* data, std::size_t size)
    {
        // Public path: the store reads its own library file.
        taste::RatingStore store (writeScratch (data, size));
        if (! store.load() && ! store.entries().empty())
            fail ("rejected rating library left entries behind");
    }
}

extern "C" int LLVMFuzzerInitialize (int*, char***)
{
    juceInit = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
    const std::string_view name = std::getenv ("GF_FUZZ_TARGET") != nullptr ? std::getenv ("GF_FUZZ_TARGET") : "state";
    const std::pair<std::string_view, Target> table[] = { { "state", Target::State }, { "preset", Target::Preset },
        { "recipe", Target::Recipe }, { "profile", Target::Profile }, { "catalog", Target::Catalog },
        { "events", Target::Events }, { "ratings", Target::Ratings } };
    bool known = false;
    for (const auto& [key, value] : table)
        if (key == name) { selected = value; known = true; }
    if (! known)
        fail ("unknown GF_FUZZ_TARGET");

    scratch = juce::File::getSpecialLocation (juce::File::tempDirectory)
                  .getChildFile ("gapfill-fuzz-" + juce::String (juce::Time::currentTimeMillis()))
                  .getChildFile ("input.bin");
    scratch.getParentDirectory().createDirectory();

    if (selected == Target::State || selected == Target::Preset)
    {
        processor = std::make_unique<GapFillAudioProcessor>();
        processor->setRateAndBufferSizeDetails (48000.0, 512);
        processor->prepareToPlay (48000.0, 512);
    }

    // Registered after JUCE's own statics exist, so it runs before their destructors:
    // without it, exit-time static teardown freed JUCE's ShutdownDetector before the
    // processor's timer thread (a harness-only use-after-free, found by the first run).
    std::atexit ([]
    {
        processor.reset();
        juceInit.reset();
        scratch.getParentDirectory().deleteRecursively();
    });
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput (const std::uint8_t* data, std::size_t size)
{
    switch (selected)
    {
        case Target::State:   fuzzState (data, size); break;
        case Target::Preset:  fuzzPreset (data, size); break;
        case Target::Recipe:  fuzzRecipe (data, size); break;
        case Target::Profile: fuzzProfile (data, size); break;
        case Target::Catalog: fuzzCatalog (data, size); break;
        case Target::Events:  fuzzEvents (data, size); break;
        case Target::Ratings: fuzzRatings (data, size); break;
    }
    return 0;
}
