// W-01 (Linux part), docs/progress/W-01.md step 3: the minimised editor-lifecycle stress test.
//
// A host may create and destroy the editor on the message thread at any time while its automation thread calls
// setValueNotifyingHost() and its audio thread runs processBlock(). pluginval at strictness 10 does exactly that on
// Windows and crashes there (issue #13); this is the part of that scenario that can run on Linux. It makes no claim
// about the Windows crash: a green result here only means this interleaving did not fail on this machine.
//
// The message loop cannot be pumped here (JUCE_MODAL_LOOPS_PERMITTED=0), so queued AsyncUpdater / timer callbacks
// are not delivered between rounds; construction, attachment listener registration, notification from foreign
// threads and destruction are what is exercised.

#include "../support/TestRig.h"

#include "PluginEditor.h"

#include <atomic>
#include <thread>

using namespace gapfill;
using namespace gftest;

namespace
{
    struct ThreadReport
    {
        std::atomic<long> blocks { 0 }, paramWrites { 0 }, nonFinite { 0 };
    };
}

GF_TEST (EditorLifecycle, editor_create_destroy_while_automating_and_processing)
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    constexpr int rounds = 40;

    GapFillAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, blockSize);
    proc.prepareToPlay (sr, blockSize);

    std::vector<juce::AudioProcessorParameter*> params;

    for (auto* p : proc.getParameters())
        params.push_back (p);

    GF_EXPECT (params.size() > 100);

    std::atomic<bool> stop { false };
    ThreadReport report;

    // "Audio thread": steady stereo noise-burst input so the engine actually schedules fills.
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        juce::Random rng (1234);
        long n = 0;

        while (! stop.load())
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (ch, i, ((n / 8) % 4 == 0) ? 0.0f : (rng.nextFloat() * 2.0f - 1.0f) * 0.5f);

            midi.clear();
            proc.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    if (! std::isfinite (buffer.getSample (ch, i)))
                        report.nonFinite++;

            ++n;
            report.blocks++;
        }
    });

    // "Automation thread": every parameter, extremes included, as a host's automation lane would write them.
    std::thread automation ([&]
    {
        juce::Random rng (99);

        while (! stop.load())
        {
            auto* p = params[(size_t) rng.nextInt ((int) params.size())];
            p->setValueNotifyingHost (rng.nextFloat() < 0.2f ? (rng.nextBool() ? 0.0f : 1.0f) : rng.nextFloat());
            report.paramWrites++;
        }
    });

    // Message thread: open and close the editor, as a host does for each plug-in window.
    for (int round = 0; round < rounds; ++round)
    {
        auto editor = std::make_unique<GapFillAudioProcessorEditor> (proc);
        editor->setSize (1100, 700);
        editor->setSize (900, 600);

        if (round % 2 == 0)
            std::this_thread::yield();

        editor.reset();
    }

    stop.store(true);
    audio.join();
    automation.join();

    GF_EXPECT_MSG (report.blocks.load() > 0, "the audio thread ran");
    GF_EXPECT_MSG (report.paramWrites.load() > 0, "the automation thread ran");
    GF_EXPECT_MSG (report.nonFinite.load() == 0, "output stayed finite while the editor opened and closed");
}
