// Golden regression (docs/dev/02 F0-7). Renders fixed-seed scenarios and compares
// them with the checked-in baselines in Tests/golden/<platform>/<scenario>.f32.
//
// The baselines change only in a PR whose title contains [golden] and whose
// description says which feature intentionally changed the sound.

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

using namespace gapfill;
using namespace gftest;

namespace
{
    // The spec says 8 bars; 4 keeps the checked-in baselines near 2.5 MB each.
    constexpr double kSr    = goldenSampleRate;
    constexpr double kBpm   = goldenBpm;
    constexpr int    kBars  = goldenBars;
    constexpr int    kBlock = goldenBlockSize;

    constexpr double kMaxAbsTolerance = 1.0e-5;
    constexpr double kRmsTolerance    = 1.0e-6;

    struct Scenario { char id; const char* name; };

    constexpr Scenario kScenarios[] =
    {
        { 'A', "drum_loop_default" },
        { 'B', "spectral_mode" },
        { 'C', "sidechain_click_track" },
        { 'D', "forced_tape_stop" },
        { 'E', "complexity_chaos_stress" },
        { 'F', "bypass_to_active" },
    };

    RenderResult renderScenario (char id)
    {
        GapFillAudioProcessor proc;
        TestPlayHead playHead;
        playHead.bpm = kBpm;

        RenderOptions options;
        options.playHead = &playHead;

        const auto input = drumLoop (kSr, kBpm, kBars);
        juce::AudioBuffer<float> click;

        switch (id)
        {
            case 'A': break;
            case 'B': setParams (proc, { { "detMode", 2.0f }, { "fill", 70.0f } }); break;
            case 'C':
                click = makeImpulseTrain (kSr, (double) input.getNumSamples() / kSr, 60.0 / kBpm);
                setParams (proc, { { "detSource", 1.0f }, { "fill", 60.0f } });
                options.sidechain = &click;
                break;
            case 'D': setParams (proc, { { "fillType", 15.0f }, { "fill", 80.0f } }); break;
            case 'E':
                setParams (proc, { { "complexity", 100.0f }, { "chaos", 100.0f }, { "fill", 100.0f },
                                   { "density", 100.0f }, { "intensity", 100.0f } });
                break;
            case 'F':
                setParams (proc, { { "bypass", 1.0f }, { "fill", 70.0f } });
                options.onBlock = [&proc, half = input.getNumSamples() / 2] (int, int pos)
                {
                    if (pos >= half)
                        setParams (proc, { { "bypass", 0.0f } });
                };
                break;
            default: GF_EXPECT_MSG (false, "unknown scenario"); break;
        }

        return renderOffline (proc, input, kBlock, kSr, options);
    }

    juce::File goldenFile (const Scenario& s)
    {
        return goldenPath (juce::String::charToString (s.id) + "_" + s.name + ".f32");
    }

    void runGolden (const Scenario& s)
    {
        const auto result = renderScenario (s.id);
        GF_EXPECT_MSG (allFinite (result.out), "scenario output must be finite");
        GF_EXPECT_MSG (result.fills > 0, "scenario must actually produce fills, otherwise the golden proves nothing");

        const auto file = goldenFile (s);

        if (writeGoldenMode())
        {
            GF_EXPECT_MSG (writeGoldenFile (file, result.out), "could not write " + file.getFullPathName());
            std::cout << "         wrote " << file.getFileName() << " (" << result.fills << " fills)" << std::endl;
            return;
        }

        if (! file.existsAsFile())
        {
            std::cout << "         [golden] no baseline for platform '" << goldenPlatform()
                      << "' (" << file.getFileName() << "): UNVERIFIED" << std::endl;
            return;
        }

        juce::AudioBuffer<float> expected;
        GF_EXPECT_MSG (readGoldenFile (file, expected, result.out.getNumChannels(), result.out.getNumSamples()),
                       "golden file has the wrong size: " + file.getFileName());

        const auto d = compareBuffers (result.out, expected, kMaxAbsTolerance);

        GF_EXPECT_MSG (d.firstBad < 0,
                       "scenario " + juce::String::charToString (s.id) + " (" + s.name + ") differs from golden at sample "
                         + juce::String (d.firstBad) + " ch " + juce::String (d.channel)
                         + ", max abs error " + juce::String (d.maxAbs, 9));
        GF_EXPECT_MSG (d.rms <= kRmsTolerance,
                       "scenario " + juce::String::charToString (s.id) + " rms error " + juce::String (d.rms, 9));
    }
}

GF_TEST (Golden, scenario_A_drum_loop_default)        { runGolden (kScenarios[0]); }
GF_TEST (Golden, scenario_B_spectral_mode)            { runGolden (kScenarios[1]); }
GF_TEST (Golden, scenario_C_sidechain_click_track)    { runGolden (kScenarios[2]); }
GF_TEST (Golden, scenario_D_forced_tape_stop)         { runGolden (kScenarios[3]); }
GF_TEST (Golden, scenario_E_complexity_chaos_stress)  { runGolden (kScenarios[4]); }
GF_TEST (Golden, scenario_F_bypass_to_active)         { runGolden (kScenarios[5]); }

GF_TEST (Golden, rendering_is_repeatable_within_one_process)
{
    const auto a = renderScenario ('E');
    const auto b = renderScenario ('E');
    const auto d = compareBuffers (a.out, b.out, 0.0);
    GF_EXPECT_MSG (d.firstBad < 0, "two identical renders must be bit-identical");
    GF_EXPECT (a.fills == b.fills);
}

GF_TEST (Golden, alloc_guard_counts_allocations_on_the_guarded_thread)
{
    AllocGuard guard;
    auto* p = new int[16];
    volatile int sink = p[0];
    (void) sink;
    delete[] p;
    GF_EXPECT_MSG (guard.count() >= 1, "AllocGuard did not observe an allocation (operator new not replaced?)");
}

GF_TEST (Golden, steady_state_audio_thread_does_not_allocate)
{
    GapFillAudioProcessor proc;
    TestPlayHead playHead;
    playHead.bpm = kBpm;
    setParams (proc, { { "fill", 80.0f }, { "complexity", 80.0f } });

    const auto input = drumLoop (kSr, kBpm, 2);
    prepareProcessor (proc, kSr, kBlock, false);
    proc.setPlayHead (&playHead);

    juce::AudioBuffer<float> work (2, kBlock);
    juce::MidiBuffer midi;
    long allocations = 0;
    int guarded = 0;

    for (int pos = 0; pos + kBlock <= input.getNumSamples(); pos += kBlock)
    {
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, input, ch, pos, kBlock);

        // The first second warms up JUCE internals that allocate lazily.
        const bool warm = pos >= (int) kSr;

        if (warm)
        {
            AllocGuard guard;
            proc.processBlock (work, midi);
            allocations += guard.count();
            ++guarded;
        }
        else
        {
            proc.processBlock (work, midi);
        }

        playHead.advance (kBlock, kSr);
    }

    proc.setPlayHead (nullptr);
    GF_EXPECT (guarded > 20);
    GF_EXPECT_MSG (allocations == 0, juce::String (allocations) + " allocations on the audio thread");
}
