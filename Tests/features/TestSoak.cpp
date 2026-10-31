// GapFill continuous wall-clock processor soak. Short smoke in the ordinary
// suite; tools/qa/soak_run.py requests and independently measures long runs.
// Audio-device xruns, audibility and real-host acceptance remain separate.
// Copyright 2026 Sheldon Davidson. SPDX-License-Identifier: AGPL-3.0-or-later
#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <random>
#include <vector>

using namespace gftest;
using namespace gapfill;

GF_TEST (Soak, continuous_processor_with_automation_transport_and_state_churn)
{
    using Clock = std::chrono::steady_clock;
    const auto duration = juce::SystemStats::getEnvironmentVariable ("GF_SOAK_SECONDS", "0.25").getDoubleValue();
    const auto warmup = juce::SystemStats::getEnvironmentVariable ("GF_SOAK_WARMUP_SECONDS", "0.05").getDoubleValue();
    const auto seed = (unsigned) juce::SystemStats::getEnvironmentVariable ("GF_SOAK_SEED", "20261031").getLargeIntValue();
    GF_EXPECT (std::isfinite (duration) && duration > 0.0);
    GF_EXPECT (std::isfinite (warmup) && warmup >= 0.0);

    constexpr double sampleRate = 48000.0;
    constexpr int maxBlock = 512;
    GapFillAudioProcessor proc;                  // one processor for the entire run
    TestPlayHead playHead;
    playHead.tsNum = 4;
    playHead.tsDen = 4;
    proc.setPlayHead (&playHead);
    prepareProcessor (proc, sampleRate, maxBlock, true);
    const auto source = drumLoop (sampleRate, 120.0, 4);
    juce::AudioBuffer<float> work (4, maxBlock);
    juce::MidiBuffer midi;
    midi.ensureSize (2048);
    std::mt19937 random (seed);
    std::vector<juce::AudioProcessorParameter*> automated;
    for (auto* parameter : proc.getParameters())
        if (auto* named = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (named->paramID != "output" && named->paramID != "wetSafety"
                && named->paramID != "bypass" && named->paramID != "dry" && named->paramID != "wet")
                automated.push_back (parameter);
    GF_EXPECT (! automated.empty());

    std::uint64_t blocks = 0, samples = 0;
    long allocations = 0;
    float peak = 0.0f;
    double worstBlockRatio = 0.0;
    int cursor = 0;
    bool measured = false;
    auto measurementStart = Clock::now();
    const auto start = measurementStart;
    double measuredSeconds = 0.0;
    while (true)
    {
        const auto now = Clock::now();
        if (! measured && std::chrono::duration<double> (now - start).count() >= warmup)
        {
            measured = true;
            measurementStart = now;
            std::cout << "GF_SOAK_BEGIN\n" << std::flush;
        }
        measuredSeconds = measured ? std::chrono::duration<double> (now - measurementStart).count() : 0.0;
        if (measured && measuredSeconds >= duration)
            break;

        const auto size = 32 << (int) (random() % 5u);
        if (blocks % 17u == 0u)
            for (int change = 0; change < 3; ++change)
                automated[random() % automated.size()]->setValueNotifyingHost ((float) (random() & 0xffffu) / 65535.0f);
        if (blocks % 257u == 0u)
            proc.selectPreset ((int) (random() % (unsigned) proc.getNumPrograms()));
        if (blocks % 509u == 0u)
        {
            juce::MemoryBlock state;
            proc.getStateInformation (state);
            proc.setStateInformation (state.getData(), (int) state.getSize());
        }
        // The level assertion uses the same safe-gain contract as IntegratedStress.
        setParams (proc, { { "output", 0.0f }, { "wetSafety", 1.0f }, { "dry", 100.0f }, { "wet", 50.0f } });
        if (blocks % 97u == 0u)
        {
            playHead.bpm = 60.0 + (double) (random() % 121u);
            playHead.playing = (random() % 5u) != 0u;
        }
        if (blocks % 193u == 0u)
            playHead.ppq = (double) (random() % 32u);       // seeks / loop wraps
        if (blocks % 16u == 0u)
            proc.getLiveControl().pump (proc);            // message-thread requests between callbacks
        work.setSize (4, size, false, false, true);
        for (int i = 0; i < size; ++i)
        {
            const auto position = (cursor + i) % source.getNumSamples();
            for (int ch = 0; ch < 4; ++ch)
                work.setSample (ch, i, source.getSample (ch % 2, position));
        }
        cursor = (cursor + size) % source.getNumSamples();
        midi.clear();
        if (blocks % 23u == 0u)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), size - 1);
        }
        const auto before = Clock::now();
        {
            AllocGuard guard;
            proc.processBlock (work, midi);
            if (measured)
                allocations += guard.count();
        }
        const auto blockSeconds = std::chrono::duration<double> (Clock::now() - before).count();
        worstBlockRatio = juce::jmax (worstBlockRatio, blockSeconds * sampleRate / (double) size);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < size; ++i)
            {
                const auto value = work.getSample (ch, i);
                GF_EXPECT_MSG (std::isfinite (value), "non-finite soak output");
                peak = juce::jmax (peak, std::abs (value));
            }
        GF_EXPECT_MSG (peak <= 2.0001f, "soak output exceeded the safe-gain +6 dBFS bound");
        playHead.advance (size, sampleRate);
        ++blocks;
        samples += (std::uint64_t) size;
    }

    // Stop scheduling and drain existing voices without destroying/restarting
    // the instance. A stuck voice fails the same process that ran the soak.
    proc.getLiveControl().panic().requestPanic();
    playHead.playing = false;
    work.setSize (4, maxBlock, false, false, true);
    for (int i = 0; i < (int) (sampleRate * 30.0) / maxBlock; ++i)
    {
        work.clear();
        midi.clear();
        proc.processBlock (work, midi);
        playHead.advance (maxBlock, sampleRate);
    }
    const auto idle = proc.getFillEngine().isIdle();
    proc.setPlayHead (nullptr);
    std::cout << std::setprecision (17) << "GF_SOAK_SUMMARY {\"measured_seconds\":" << measuredSeconds
              << ",\"blocks\":" << blocks << ",\"processed_samples\":" << samples
              << ",\"peak\":" << peak << ",\"allocations\":" << allocations
              << ",\"engine_idle\":" << (idle ? "true" : "false")
              << ",\"worst_block_duration_ratio\":" << worstBlockRatio
              << ",\"sample_rate\":" << sampleRate << ",\"seed\":" << seed << "}\n" << std::flush;
    GF_EXPECT_MSG (allocations == 0, "allocation in a measured soak callback");
    GF_EXPECT_MSG (idle, "voices did not clear after Panic and 30 seconds of silent processing");
}
