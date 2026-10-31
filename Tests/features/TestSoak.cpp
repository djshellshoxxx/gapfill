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
#include <memory>
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
    auto partner = std::make_unique<GapFillAudioProcessor>();
    TestPlayHead partnerPlayHead;
    partnerPlayHead.tsNum = 4;
    partnerPlayHead.tsDen = 4;
    partner->setPlayHead (&partnerPlayHead);
    prepareProcessor (*partner, sampleRate, maxBlock, true);
    setParams (*partner, { { "fill", 80.0f }, { "coopGroup", 1.0f }, { "coopMode", 1.0f },
                          { "output", 0.0f }, { "wetSafety", 1.0f }, { "dry", 100.0f }, { "wet", 50.0f } });
    auto& scenes = proc.getLiveControl().scenes();
    GF_EXPECT (scenes.saveCurrent (0, "Soak A"));
    setParams (proc, { { "fill", 80.0f } });
    GF_EXPECT (scenes.saveCurrent (1, "Soak B"));
    auto& recording = proc.getUiState().recordTap;
    GF_EXPECT (recording.allocate (sampleRate));
    auto& exportService = proc.getRecipes().exportService;
    const auto source = drumLoop (sampleRate, 120.0, 4);
    juce::AudioBuffer<float> work (4, maxBlock);
    juce::AudioBuffer<float> partnerWork (4, maxBlock);
    juce::AudioBuffer<float> exported (2, (int) (sampleRate * 3.2));
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
    std::uint64_t sceneRequests = 0, captureCommands = 0, recordCancels = 0;
    std::uint64_t exportRequests = 0, exportCancels = 0, exportResults = 0;
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
        setParams (proc, { { "coopGroup", 1.0f } });
        if (blocks % 89u == 0u)
            if (scenes.gotoScene ((int) ((blocks / 89u) % 2u)))
                ++sceneRequests;
        if (blocks % 113u == 0u)
        {
            setParams (proc, { { "capture", 1.0f } });
            if (proc.postCommand ({ CommandType::Recapture }))
                ++captureCommands;
        }
        if (blocks % 113u == 1u)
            if (proc.postCommand ({ CommandType::ReleaseCapture }))
                ++captureCommands;
        if (blocks % 173u == 0u)
            recording.start (4, sampleRate);
        if (blocks % 173u == 1u)
        {
            recording.cancel();
            ++recordCancels;
        }
        if (blocks % 97u == 0u)
        {
            playHead.bpm = 60.0 + (double) (random() % 121u);
            playHead.playing = (random() % 5u) != 0u;
        }
        if (blocks % 193u == 0u)
            playHead.ppq = (double) (random() % 32u);       // seeks / loop wraps
        if (blocks % 16u == 0u)
        {
            proc.getLiveControl().pump (proc);            // message-thread requests between callbacks
            proc.drainFillEvents();
            exportService.arm();
            exporting::Failure reason = exporting::Failure::None;
            if (exportService.take (exported, reason))
            {
                GF_EXPECT (allFinite (exported));
                ++exportResults;
            }
        }
        if (blocks % 127u == 0u && proc.getRecipeHistory().size() > 0)
        {
            const auto span = proc.getRecipeHistory().getSpan (0);
            if (exportService.request (span) == exporting::RequestResult::Accepted)
            {
                ++exportRequests;
                if (blocks % 254u == 0u && exportService.cancelPending())
                    ++exportCancels;
            }
        }
        work.setSize (4, size, false, false, true);
        partnerWork.setSize (4, size, false, false, true);
        for (int i = 0; i < size; ++i)
        {
            const auto position = (cursor + i) % source.getNumSamples();
            for (int ch = 0; ch < 4; ++ch)
            {
                const auto value = source.getSample (ch % 2, position);
                work.setSample (ch, i, value);
                partnerWork.setSample (ch, i, value);
            }
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
            partnerPlayHead.bpm = playHead.bpm;
            partnerPlayHead.ppq = playHead.ppq;
            partnerPlayHead.playing = playHead.playing;
            partnerPlayHead.timeInSamples = playHead.timeInSamples;
            partner->processBlock (partnerWork, midi);
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
                const auto other = partnerWork.getSample (ch, i);
                GF_EXPECT_MSG (std::isfinite (other), "non-finite cooperative partner output");
                peak = juce::jmax (peak, juce::jmax (std::abs (value), std::abs (other)));
            }
        GF_EXPECT_MSG (peak <= 2.0001f, "soak output exceeded the safe-gain +6 dBFS bound");
        playHead.advance (size, sampleRate);
        ++blocks;
        samples += (std::uint64_t) size;
    }

    // Stop scheduling and drain existing voices without destroying/restarting
    // the instance. A stuck voice fails the same process that ran the soak.
    proc.getLiveControl().panic().requestPanic();
    partner->getLiveControl().panic().requestPanic();
    recording.cancel();
    exportService.cancelPending();
    exportService.disarm();
    playHead.playing = false;
    partnerPlayHead.playing = false;
    work.setSize (4, maxBlock, false, false, true);
    for (int i = 0; i < (int) (sampleRate * 30.0) / maxBlock; ++i)
    {
        work.clear();
        midi.clear();
        proc.processBlock (work, midi);
        partner->processBlock (work, midi);
        playHead.advance (maxBlock, sampleRate);
        partnerPlayHead.advance (maxBlock, sampleRate);
    }
    const auto idle = proc.getFillEngine().isIdle() && partner->getFillEngine().isIdle();
    proc.setPlayHead (nullptr);
    partner->setPlayHead (nullptr);
    std::cout << std::setprecision (17) << "GF_SOAK_SUMMARY {\"measured_seconds\":" << measuredSeconds
              << ",\"blocks\":" << blocks << ",\"processed_samples\":" << samples
              << ",\"peak\":" << peak << ",\"allocations\":" << allocations
              << ",\"engine_idle\":" << (idle ? "true" : "false")
              << ",\"worst_block_duration_ratio\":" << worstBlockRatio
              << ",\"instances\":2,\"partner_callbacks\":\"serial\""
              << ",\"scene_requests\":" << sceneRequests
              << ",\"scene_commits\":" << scenes.getCommitCount()
              << ",\"capture_commands\":" << captureCommands
              << ",\"record_cancellations\":" << recordCancels
              << ",\"export_requests\":" << exportRequests
              << ",\"export_cancellations\":" << exportCancels
              << ",\"export_results\":" << exportResults
              << ",\"sample_rate\":" << sampleRate << ",\"seed\":" << seed << "}\n" << std::flush;
    GF_EXPECT_MSG (allocations == 0, "allocation in a measured soak callback");
    GF_EXPECT_MSG (idle, "voices did not clear after Panic and 30 seconds of silent processing");
}
