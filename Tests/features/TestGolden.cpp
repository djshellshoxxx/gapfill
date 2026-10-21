// Golden regression (docs/dev/02 F0-7). Renders fixed-seed scenarios and compares
// them with the checked-in baselines in Tests/golden/<platform>/<scenario>.f32.
//
// The baselines change only in a PR whose title contains [golden] and whose
// description says which feature intentionally changed the sound.

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include <algorithm>
#include <cmath>
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

    // Tolerances are per scenario because libm / FMA / vectorisation differ
    // between machines and compilers (docs/dev/golden-tolerances.md has the
    // evidence). Scenarios that go through spectral analysis or chaotic
    // selection (B, E) amplify last-ulp differences into small waveform
    // differences, so they get a looser sample tolerance PLUS machine-independent
    // invariants (energy, peak, DC, per-block envelope) that still catch a real
    // behaviour change. Every other scenario stays tight.
    struct Scenario { char id; const char* name; double maxAbs; double rms; bool sensitive; };

    constexpr Scenario kScenarios[] =
    {
        { 'A', "drum_loop_default",        1.0e-5, 1.0e-6, false },
        { 'B', "spectral_mode",            1.0e-2, 1.0e-3, true  },
        { 'C', "sidechain_click_track",    1.0e-5, 1.0e-6, false },
        { 'D', "forced_tape_stop",         1.0e-5, 1.0e-6, false },
        { 'E', "complexity_chaos_stress",  1.0e-2, 1.0e-3, true  },
        { 'F', "bypass_to_active",         1.0e-5, 1.0e-6, false },
    };


    struct SignalStats { double rms = 0.0, peak = 0.0, dc = 0.0; };

    SignalStats signalStats (const juce::AudioBuffer<float>& b)
    {
        SignalStats st;
        double sumSq = 0.0, sum = 0.0;
        const auto n = (double) b.getNumChannels() * (double) b.getNumSamples();

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double v = b.getSample (ch, i);
                sumSq += v * v;
                sum += v;
                st.peak = std::max (st.peak, std::abs (v));
            }

        st.rms = n > 0 ? std::sqrt (sumSq / n) : 0.0;
        st.dc = n > 0 ? sum / n : 0.0;
        return st;
    }

    /** First 512-sample block (channel-summed energy) whose level differs by more than
        1 dB, ignoring blocks quieter than -60 dBFS in both. -1 when none. This pins
        where fills start and stop without depending on exact waveform values. */
    int envelopeMismatchBlock (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        constexpr int block = 512;
        const auto blocks = a.getNumSamples() / block;

        for (int k = 0; k < blocks; ++k)
        {
            double ea = 0.0, eb = 0.0;

            for (int ch = 0; ch < a.getNumChannels(); ++ch)
                for (int i = k * block; i < (k + 1) * block; ++i)
                {
                    ea += (double) a.getSample (ch, i) * a.getSample (ch, i);
                    eb += (double) b.getSample (ch, i) * b.getSample (ch, i);
                }

            const auto da = 10.0 * std::log10 (ea / block + 1.0e-12);
            const auto db = 10.0 * std::log10 (eb / block + 1.0e-12);

            if ((da > -60.0 || db > -60.0) && std::abs (da - db) > 1.0)
                return k;
        }

        return -1;
    }

    RenderResult renderScenario (char id) { return renderGoldenScenario (id); }

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

        const auto d = compareBuffers (result.out, expected, s.maxAbs);

        GF_EXPECT_MSG (d.firstBad < 0,
                       "scenario " + juce::String::charToString (s.id) + " (" + s.name + ") differs from golden at sample "
                         + juce::String (d.firstBad) + " ch " + juce::String (d.channel)
                         + ", max abs error " + juce::String (d.maxAbs, 9));
        GF_EXPECT_MSG (d.rms <= s.rms,
                       "scenario " + juce::String::charToString (s.id) + " rms error " + juce::String (d.rms, 9));

        // Machine-independent invariants, checked for every scenario (they are what
        // protects the loosely toleranced ones).
        const auto sa = signalStats (result.out);
        const auto sb = signalStats (expected);
        const auto label = juce::String::charToString (s.id) + " (" + s.name + ")";
        GF_EXPECT_MSG (std::abs (sa.rms - sb.rms) <= 0.02 * sb.rms + 1.0e-9, "scenario " + label + " RMS changed");
        GF_EXPECT_MSG (std::abs (sa.peak - sb.peak) <= 0.05 * sb.peak + 1.0e-9, "scenario " + label + " peak changed");
        GF_EXPECT_MSG (std::abs (sa.dc - sb.dc) <= 1.0e-3, "scenario " + label + " DC offset changed");
        GF_EXPECT_MSG (envelopeMismatchBlock (result.out, expected) < 0,
                       "scenario " + label + " block energy envelope (event timing) differs at block "
                         + juce::String (envelopeMismatchBlock (result.out, expected)));
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
