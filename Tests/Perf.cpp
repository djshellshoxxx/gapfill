// Performance and memory report (docs/dev/02 F0-7, docs/dev/06 section 5).
// Group Perf: prints numbers; wall-clock enforcement is opt-in (GAPFILL_PERF_ENFORCE=1).

#include "../Tests/support/AllocGuard.h"
#include "support/TestRig.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kSr    = goldenSampleRate;
    constexpr double kBpm   = goldenBpm;
    constexpr int    kBars  = goldenBars;
    constexpr int    kBlock = goldenBlockSize;
}

GF_TEST (Perf, memory_footprint_report)
{
    std::cout << "         sizeof(FillVoice) = " << sizeof (FillVoice)
              << " bytes, sizeof(FillEngine) = " << sizeof (FillEngine)
              << " bytes (" << FillEngine::numVoices << " voices)" << std::endl;

    for (const double sr : { 48000.0, 192000.0 })
    {
        GapFillAudioProcessor proc;
        AllocGuard guard;
        prepareProcessor (proc, sr, 512, true);
        std::cout << "         prepareToPlay(" << (int) sr << " Hz, 512, stereo + sidechain) requested "
                  << guard.bytes() / 1024 << " KiB in " << guard.count() << " allocations" << std::endl;
        GF_EXPECT (guard.bytes() > 0);
    }
}

GF_TEST (Perf, scenario_E_block_time_baseline)
{
    // Reports mean and p99.9 block time for scenario E. Enforcement against
    // Tests/perf_baseline.json is opt-in (GAPFILL_PERF_ENFORCE=1) because wall
    // clock budgets are meaningless on shared CI machines.
    GapFillAudioProcessor proc;
    TestPlayHead playHead;
    playHead.bpm = kBpm;
    setParams (proc, { { "complexity", 100.0f }, { "chaos", 100.0f }, { "fill", 100.0f },
                       { "density", 100.0f }, { "intensity", 100.0f } });

    const auto input = drumLoop (kSr, kBpm, kBars);
    prepareProcessor (proc, kSr, kBlock, false);
    proc.setPlayHead (&playHead);

    juce::AudioBuffer<float> work (2, kBlock);
    juce::MidiBuffer midi;
    std::vector<double> micros;

    for (int pos = 0; pos + kBlock <= input.getNumSamples(); pos += kBlock)
    {
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, input, ch, pos, kBlock);

        const auto t0 = std::chrono::steady_clock::now();
        proc.processBlock (work, midi);
        const auto t1 = std::chrono::steady_clock::now();
        micros.push_back (std::chrono::duration<double, std::micro> (t1 - t0).count());
        playHead.advance (kBlock, kSr);
    }

    proc.setPlayHead (nullptr);
    GF_EXPECT (! micros.empty());

    double sum = 0.0;
    for (const auto m : micros)
        sum += m;

    std::sort (micros.begin(), micros.end());
    const auto mean = sum / (double) micros.size();
    const auto p999 = micros[(size_t) juce::jmin ((double) micros.size() - 1.0, std::floor ((double) micros.size() * 0.999))];
    const auto budgetMicros = 1.0e6 * kBlock / kSr;

    std::cout << "         block time at " << kBlock << " samples/48 kHz: mean " << juce::String (mean, 1)
              << " us, p99.9 " << juce::String (p999, 1) << " us (block = " << juce::String (budgetMicros, 0)
              << " us, cpu mean " << juce::String (100.0 * mean / budgetMicros, 2) << " %)" << std::endl;

    GF_EXPECT_MSG (p999 < budgetMicros, "p99.9 block time exceeds the block duration");

    if (juce::SystemStats::getEnvironmentVariable ("GAPFILL_PERF_ENFORCE", {}) == "1")
    {
        const auto baseline = juce::JSON::parse (testDataDir().getChildFile ("perf_baseline.json"));
        const double baseMean = baseline.getProperty ("meanMicros", 0.0);
        GF_EXPECT_MSG (baseMean > 0.0, "perf_baseline.json missing meanMicros");
        GF_EXPECT_MSG (mean <= baseMean * 1.10, "mean block time more than 10 % above baseline");
    }
}
