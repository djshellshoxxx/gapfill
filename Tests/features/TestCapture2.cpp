// C-10 Capture v2 (docs/dev/04-SPEC-CREATIVE.md C-10).

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include "features/capture2/CaptureLatch.h"

#include <cmath>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 512;

    /** Deterministic, position-unique test signal. */
    float signalAt (std::int64_t i)
    {
        return 0.5f * std::sin ((float) (i % 100003) * 0.0123f) + 0.25f * (float) ((i / 997) % 7) / 7.0f;
    }

    /** Drives a latch and a history the way processChunk does. */
    struct Harness
    {
        SourceHistory history;
        CaptureLatch latch;
        juce::AudioBuffer<float> block { 2, kBlock };
        std::int64_t global = 0;
        bool held = false;
        int swaps = 0;
        int maxCopy = 0;

        explicit Harness (double sr = kSr)
        {
            history.prepare (2, (int) (6.0 * sr));
            latch.prepare (sr, 2);
        }

        void run (int blocks, bool heldNow)
        {
            held = heldNow;

            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < kBlock; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        block.setSample (ch, i, signalAt (global + i));

                if (! held)
                    history.write (block, 0, kBlock);

                swaps += latch.update (held, block, 0, kBlock, history) ? 1 : 0;
                maxCopy = std::max (maxCopy, latch.getLastCopyCount());
                global += kBlock;
            }
        }
    };
}

GF_TEST (Capture2, default_window_changes_nothing_for_the_plugin)
{
    // With captureLength 1000 ms and no recapture the latched sound equals a 2000 ms window:
    // fills never read further than ~0.4 s before the freeze point.
    auto render = [] (float lengthMs)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 40.0f }, { "capture", 1.0f },
                           { "captureLength", lengthMs } });
        return renderOffline (proc, drumLoop (kSr, 120.0, 6), kBlock, kSr);
    };

    const auto a = render (1000.0f);
    const auto b = render (2000.0f);
    GF_EXPECT (a.fills > 0);
    GF_EXPECT (compareBuffers (a.out, b.out, 0.0).maxAbs == 0.0);

    // A short window does change what the fills can read.
    const auto c = render (250.0f);
    GF_EXPECT (compareBuffers (a.out, c.out, 0.0).maxAbs > 0.0);
}

GF_TEST (Capture2, recapture_replaces_material)
{
    Harness h;
    h.run (200, false);                       // 2.1 s of live input
    h.run (50, true);                         // freeze
    GF_EXPECT (h.latch.isHeld());
    GF_EXPECT (h.latch.getActiveSnapshot() == nullptr);
    GF_EXPECT (h.latch.getStatus() == CaptureLatch::Held);

    h.run (100, true);                        // 5 s of frozen history while the live tap keeps recording
    const auto endAtRequest = h.global;       // the live tap's write position when the request is taken
    h.latch.requestRecapture();

    int blocks = 0;

    while (h.latch.getActiveSnapshot() == nullptr && blocks++ < 200)
        h.run (1, true);

    const auto* snap = h.latch.getActiveSnapshot();
    GF_EXPECT (snap != nullptr);
    GF_EXPECT (h.swaps == 1);
    GF_EXPECT (h.latch.getStatus() == CaptureLatch::Held);

    // The snapshot holds the live input from the request time: the last 1000 ms before 'endAtRequest' + one block
    // (the request is taken inside the next update, after that block was recorded).
    const auto windowEnd = endAtRequest + kBlock;
    const auto windowSamples = h.latch.getWindowSamples();
    GF_EXPECT ((int) snap->getWritePosition() == windowSamples);

    for (int i : { 0, 1, 777, 20000, windowSamples - 1 })
        GF_EXPECT_NEAR (snap->read (0, (double) i), signalAt (windowEnd - windowSamples + i), 1.0e-6);

    // The frozen history was NOT replaced (v1 behaviour: writer paused) and keeps its floor off while a snapshot is active.
    GF_EXPECT (h.history.getReadFloor() == 0);
}

GF_TEST (Capture2, pending_uses_live_until_the_swap)
{
    Harness h;
    h.run (200, false);
    h.run (10, true);

    h.latch.requestRecapture();
    h.run (1, true);
    GF_EXPECT (h.latch.getStatus() == CaptureLatch::Pending);
    GF_EXPECT (h.latch.isPending());
    GF_EXPECT (h.latch.getActiveSnapshot() == nullptr);          // the frozen/live history is still the source
    GF_EXPECT (h.latch.getProgress() > 0.0f && h.latch.getProgress() < 1.0f);

    // A second recapture replaces the pending copy, and the previous snapshot stays active meanwhile.
    h.run (50, true);
    const auto* first = h.latch.getActiveSnapshot();
    GF_EXPECT (first != nullptr);

    h.latch.requestRecapture();
    h.run (2, true);
    GF_EXPECT (h.latch.isPending());
    GF_EXPECT (h.latch.getActiveSnapshot() == first);

    h.run (60, true);
    GF_EXPECT (! h.latch.isPending());
    GF_EXPECT (h.latch.getActiveSnapshot() != nullptr && h.latch.getActiveSnapshot() != first);
    GF_EXPECT (h.swaps == 2);
}

GF_TEST (Capture2, copy_is_bounded_per_block)
{
    for (double sr : { 44100.0, 48000.0, 192000.0 })
    {
        Harness h (sr);
        h.latch.setLengthMs (2000.0f);
        h.run ((int) (3.0 * sr / kBlock) + 4, false);
        h.run (5, true);
        h.maxCopy = 0;
        h.latch.requestRecapture();

        int blocks = 0;

        while (h.latch.getActiveSnapshot() == nullptr && blocks++ < 4000)
            h.run (1, true);

        const auto total = (int) std::lround (2.0 * sr);
        GF_EXPECT_MSG (h.maxCopy <= CaptureLatch::kMaxCopyPerBlock, "copy of " + juce::String (h.maxCopy) + " samples in one block");
        GF_EXPECT_MSG (blocks >= total / CaptureLatch::kMaxCopyPerBlock, "finished too quickly at " + juce::String (sr));
        GF_EXPECT (h.latch.getActiveSnapshot() != nullptr);
    }
}

GF_TEST (Capture2, release_cancels_a_pending_copy_and_returns_to_live)
{
    Harness h;
    h.run (200, false);
    h.run (10, true);
    h.latch.requestRecapture();
    h.run (2, true);
    GF_EXPECT (h.latch.isPending());

    h.run (1, false);                          // Capture off
    GF_EXPECT (! h.latch.isPending());
    GF_EXPECT (h.latch.getStatus() == CaptureLatch::Off);
    GF_EXPECT (h.latch.getActiveSnapshot() == nullptr);
    GF_EXPECT (h.history.getReadFloor() == 0);
    GF_EXPECT (h.swaps == 0);

    // Releasing from an active snapshot reports a source change (the engine fades Granular voices).
    h.run (10, true);
    h.latch.requestRecapture();
    h.run (60, true);
    GF_EXPECT (h.latch.getActiveSnapshot() != nullptr);
    const auto before = h.swaps;
    h.run (1, false);
    GF_EXPECT (h.swaps == before + 1);
    GF_EXPECT (h.latch.getActiveSnapshot() == nullptr);

    // Recapture while Capture is off is ignored.
    h.latch.requestRecapture();
    h.run (80, false);
    GF_EXPECT (h.latch.getActiveSnapshot() == nullptr && ! h.latch.isPending());
    h.run (1, true);
    GF_EXPECT (! h.latch.isPending());       // the stale request did not survive
}

GF_TEST (Capture2, release_command_suppresses_until_the_parameter_falls)
{
    CaptureLatch latch;
    latch.prepare (kSr, 2);
    latch.noteParameter (true);
    GF_EXPECT (! latch.isSuppressed());
    latch.requestRelease();
    GF_EXPECT (latch.isSuppressed());
    latch.noteParameter (true);
    GF_EXPECT (latch.isSuppressed());
    latch.noteParameter (false);
    GF_EXPECT (! latch.isSuppressed());
}

GF_TEST (Capture2, sample_rate_change_and_reset_clear_the_latch)
{
    Harness h;
    h.run (200, false);
    h.run (10, true);
    h.latch.requestRecapture();
    h.run (60, true);
    GF_EXPECT (h.latch.getActiveSnapshot() != nullptr && h.latch.isHeld());

    h.latch.prepare (96000.0, 2);
    GF_EXPECT (h.latch.getActiveSnapshot() == nullptr);
    GF_EXPECT (! h.latch.isHeld() && ! h.latch.isPending());
    GF_EXPECT (h.latch.getStatus() == CaptureLatch::Off);
    GF_EXPECT (h.latch.getLiveTap().getWritePosition() == 0);

    h.latch.reset();
    GF_EXPECT (h.latch.getSnapshotCount() >= 0);

    // And through the whole plugin: a new prepareToPlay clears it.
    GapFillAudioProcessor proc;
    setParams (proc, { { "capture", 1.0f }, { "fill", 80.0f } });
    renderOffline (proc, drumLoop (kSr, 120.0, 3), kBlock, kSr);
    proc.postCommand ({ CommandType::Recapture });
    renderOffline (proc, drumLoop (kSr, 120.0, 3), kBlock, kSr);   // prepares again: the latch starts over
    GF_EXPECT (proc.getShaping().capture.getSnapshotCount() <= 1);
}

GF_TEST (Capture2, reads_below_the_floor_and_beyond_the_end_are_never_garbage)
{
    SourceHistory hist;
    hist.prepare (2, 48000);
    juce::AudioBuffer<float> block (2, 1000);

    for (int b = 0; b < 20; ++b)
    {
        for (int i = 0; i < 1000; ++i)
            for (int ch = 0; ch < 2; ++ch)
                block.setSample (ch, i, signalAt (b * 1000 + i));

        hist.write (block, 0, 1000);
    }

    hist.setReadFloor (15000);
    GF_EXPECT (! hist.isPositionValid (14999) && hist.isPositionValid (15000) && hist.isPositionValid (19000));
    GF_EXPECT_NEAR (hist.read (0, 100.0), signalAt (15000), 1.0e-6);       // clamped to the floor sample
    GF_EXPECT_NEAR (hist.read (0, 999999.0), signalAt (19999), 1.0e-6);    // clamped to the newest sample

    juce::AudioBuffer<float> dest (2, 2000);
    hist.copyTo (dest, 14000, 2000);                                      // 14000..15999: first 1000 are below the floor

    for (int i = 0; i < 1000; ++i)
        GF_EXPECT (dest.getSample (0, i) == 0.0f);

    GF_EXPECT_NEAR (dest.getSample (0, 1000), signalAt (15000), 1.0e-6);

    hist.setReadFloor (0);
    hist.copyTo (dest, 14000, 2000);
    GF_EXPECT_NEAR (dest.getSample (0, 0), signalAt (14000), 1.0e-6);

    hist.clear();
    GF_EXPECT (hist.getReadFloor() == 0);
}

GF_TEST (Capture2, recapture_through_the_plugin_stays_finite_and_bounded)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 40.0f }, { "capture", 1.0f },
                       { "wGranular", 100.0f }, { "dry", 0.0f }, { "wet", 100.0f } });

    int block = 0;
    RenderOptions opt;
    opt.onBlock = [&] (int index, int)
    {
        block = index;

        if (index == 150 || index == 400)
            proc.postCommand ({ CommandType::Recapture });
    };

    const auto result = renderOffline (proc, drumLoop (kSr, 120.0, 8), kBlock, kSr, opt);
    GF_EXPECT (block > 400);
    GF_EXPECT (result.fills > 0);
    GF_EXPECT (proc.getShaping().capture.getSnapshotCount() == 2);
    GF_EXPECT (proc.getShaping().capture.getMaxCopyCount() <= CaptureLatch::kMaxCopyPerBlock);

    for (int ch = 0; ch < result.out.getNumChannels(); ++ch)
        for (int i = 0; i < result.out.getNumSamples(); ++i)
        {
            const auto v = result.out.getSample (ch, i);
            GF_EXPECT (std::isfinite (v) && std::abs (v) < 8.0f);
        }
}

GF_TEST (Capture2, no_audio_thread_allocation_while_capturing_and_recapturing)
{
    GapFillAudioProcessor proc;
    TestPlayHead playHead;
    playHead.bpm = 120.0;
    setParams (proc, { { "fill", 80.0f }, { "capture", 1.0f }, { "captureLength", 2000.0f } });

    const auto input = drumLoop (kSr, 120.0, 6);
    prepareProcessor (proc, kSr, kBlock, false);
    proc.setPlayHead (&playHead);

    juce::AudioBuffer<float> work (2, kBlock);
    juce::MidiBuffer midi;
    long allocations = 0;
    int guarded = 0, index = 0;

    for (int pos = 0; pos + kBlock <= input.getNumSamples(); pos += kBlock, ++index)
    {
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, input, ch, pos, kBlock);

        if (pos >= (int) kSr * 2)
        {
            if (index % 120 == 0)
                proc.postCommand ({ CommandType::Recapture });

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
    GF_EXPECT (guarded > 50);
    GF_EXPECT_MSG (allocations == 0, "audio thread allocated " + juce::String ((int) allocations) + " times");
    GF_EXPECT (proc.getShaping().capture.getSnapshotCount() >= 1);
}
