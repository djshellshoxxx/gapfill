// U-2 Fill Export (docs/dev/05-SPEC-USABILITY-VALUE.md U-2).

#include "../support/AllocGuard.h"
#include "../support/RecipeRigs.h"

#include "features/export/ExportService.h"
#include "features/export/WavExport.h"

using namespace gapfill;
using namespace gftest;
using namespace recipetest;
using namespace gapfill::exporting;

namespace
{
    /** Writes `blocks` x `n` samples of a position-coded ramp through the tap: sample at position p has value p * 1e-6. */
    void feedRamp (ExportService& svc, std::int64_t& pos, int blocks, int n = kBlock)
    {
        juce::AudioBuffer<float> wet (2, n);

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < n; ++i)
            {
                wet.setSample (0, i, (float) ((pos + i) % 100000) * 1.0e-5f);
                wet.setSample (1, i, -(float) ((pos + i) % 100000) * 1.0e-5f);
            }

            svc.process (wet, 0, n, pos);
            pos += n;
        }
    }

    bool waitReady (ExportService& svc, std::int64_t& pos, int maxBlocks, juce::AudioBuffer<float>& out, Failure& why)
    {
        for (int i = 0; i < maxBlocks; ++i)
        {
            feedRamp (svc, pos, 1);

            if (svc.take (out, why))
                return true;

            if (why != Failure::None)
                return false;
        }

        return false;
    }
}

GF_TEST (Export, export_segment_matches_wet_bus)
{
    // Listen to the wet bus alone (dry 0, wet 100): the output then IS the wet bus, delayed nowhere.
    ProcRun run ({ { "fill", 90.0f }, { "density", 90.0f }, { "dry", 0.0f }, { "wet", 100.0f } });
    auto& svc = run.proc.getRecipes().exportService;
    svc.arm();

    const auto input = drumLoop (kSr, 150.0, 4);
    std::vector<float> outL, outR;

    for (int i = 0; i * kBlock + kBlock <= input.getNumSamples(); ++i)
    {
        const auto& out = run.step (input);

        for (int k = 0; k < kBlock; ++k)
        {
            outL.push_back (out.getSample (0, k));
            outR.push_back (out.getSample (1, k));
        }
    }

    run.proc.drainFillEvents();
    const auto& h = run.proc.getRecipeHistory();
    GF_EXPECT (h.size() >= 2);

    const auto latency = run.proc.getActiveLatencySamples();
    const auto lead = (std::int64_t) std::llround (kLeadInSeconds * kSr);

    for (int which = 0; which < juce::jmin (3, h.size()); ++which)
    {
        const auto span = h.getSpan (which);
        GF_EXPECT (span.isKnown());
        GF_EXPECT (svc.request (span) == RequestResult::Accepted);

        // keep the audio running a little so the mailbox is serviced
        const auto silence = makeSilence (2, kBlock * 80);
        juce::AudioBuffer<float> exported;
        Failure why = Failure::None;
        bool got = false;

        run.blockIndex = 0;

        for (int i = 0; i < 80 && ! got; ++i)
        {
            run.step (silence);
            got = svc.take (exported, why);
        }

        GF_EXPECT_MSG (got, "segment " + juce::String (which) + " became ready");

        if (! got)
            continue;

        GF_EXPECT (exported.getNumChannels() == 2);
        GF_EXPECT (exported.getNumSamples() == (int) (lead + span.lengthSamples + (std::int64_t) std::llround (kTailSeconds * kSr)));

        int mismatches = 0;

        for (int k = 0; k < exported.getNumSamples(); ++k)
        {
            const auto idx = (std::int64_t) (span.startPos - lead + k) + latency;

            if (idx < 0 || idx >= (std::int64_t) outL.size())
                continue;

            if (std::abs (exported.getSample (0, k) - outL[(size_t) idx]) > 1.0e-6f
                 || std::abs (exported.getSample (1, k) - outR[(size_t) idx]) > 1.0e-6f)
                ++mismatches;
        }

        GF_EXPECT_MSG (mismatches == 0, "exported samples equal the wet bus, mismatches: " + juce::String (mismatches));
        GF_EXPECT (regionEnergy (exported, 0, exported.getNumSamples()) > 0.0f);
    }
}

GF_TEST (Export, export_disarmed_zero_cost)
{
    ExportService svc;
    svc.prepare (kSr);
    std::int64_t pos = 0;

    feedRamp (svc, pos, 20);
    GF_EXPECT_MSG (svc.getSamplesWritten() == 0, "never armed: the ring is not written");
    GF_EXPECT (! svc.isArmed());

    svc.arm();
    feedRamp (svc, pos, 10);
    GF_EXPECT (svc.getSamplesWritten() == 10 * kBlock);

    svc.disarm();
    feedRamp (svc, pos, 10);
    GF_EXPECT (svc.getSamplesWritten() == 10 * kBlock);

    // The armed window expires by itself.
    svc.armForMs (1);
    juce::Thread::sleep (8);
    feedRamp (svc, pos, 5);
    GF_EXPECT_MSG (svc.getSamplesWritten() == 10 * kBlock && ! svc.isArmed(), "an expired window disarms the tap");

    // Re-arming starts a fresh run: older audio is not available.
    svc.arm();
    feedRamp (svc, pos, 5);
    GF_EXPECT (svc.getFirstPos() == pos - 5 * kBlock);
}

GF_TEST (Export, export_too_old_rejected)
{
    ExportService svc;
    svc.prepare (kSr);
    svc.arm();
    std::int64_t pos = 0;
    feedRamp (svc, pos, 900);                       // 9.6 s: more than the 8 s ring

    juce::AudioBuffer<float> out;
    Failure why = Failure::None;

    GF_EXPECT (svc.request ({ 1000, 4800 }) == RequestResult::Accepted);
    GF_EXPECT (! waitReady (svc, pos, 3, out, why));
    GF_EXPECT_MSG (why == Failure::TooOld, "older than the ring");
    GF_EXPECT (failureText (why) == "That fill is too old");
    GF_EXPECT (svc.getState() == ExportState::Idle);

    // Not recorded: it ended before the tap was armed.
    ExportService late;
    late.prepare (kSr);
    std::int64_t p2 = 0;
    feedRamp (late, p2, 20);
    late.arm();
    feedRamp (late, p2, 20);
    GF_EXPECT (late.request ({ 100, 4800 }) == RequestResult::Accepted);
    GF_EXPECT (! waitReady (late, p2, 3, out, why));
    GF_EXPECT (why == Failure::NotRecorded);

    // A span from the future (a timeline reset) is not recorded either, and a span of zero length is refused.
    GF_EXPECT (late.request ({ p2 + 100000, 4800 }) == RequestResult::Accepted);
    GF_EXPECT (! waitReady (late, p2, 3, out, why) && why == Failure::NotRecorded);
    GF_EXPECT (late.request ({ 10, 0 }) == RequestResult::NoSpan);
}

GF_TEST (Export, export_bounded_copy_per_block)
{
    ExportService svc;
    svc.prepare (kSr);
    svc.arm();
    std::int64_t pos = 0;
    feedRamp (svc, pos, 400);                         // 4.3 s recorded

    const std::int64_t length = (std::int64_t) (2.5 * kSr);
    const auto startPos = pos - length - 6000;
    GF_EXPECT (svc.request ({ startPos, (int) length }) == RequestResult::Accepted);

    juce::AudioBuffer<float> out;
    Failure why = Failure::None;
    int blocks = 0;

    while (blocks < 200 && ! svc.take (out, why))
    {
        feedRamp (svc, pos, 1);
        ++blocks;
    }

    GF_EXPECT (why == Failure::None);
    GF_EXPECT (svc.getMaxCopyPerBlock() > 0 && svc.getMaxCopyPerBlock() <= kCopyPerBlock);
    GF_EXPECT_MSG (blocks >= (int) (length / kCopyPerBlock), "a long segment takes many blocks: " + juce::String (blocks));
    GF_EXPECT (out.getNumSamples() > length);

    // Position-coded check: sample k of the segment is the ramp value of its absolute position.
    const auto lead = (std::int64_t) std::llround (kLeadInSeconds * kSr);
    int wrong = 0;

    for (int k = 0; k < out.getNumSamples(); ++k)
    {
        const auto p = startPos - lead + k;
        const auto expected = (float) (p % 100000) * 1.0e-5f;

        if (std::abs (out.getSample (0, k) - expected) > 1.0e-6f || std::abs (out.getSample (1, k) + expected) > 1.0e-6f)
            ++wrong;
    }

    GF_EXPECT_MSG (wrong == 0, "the segment holds exactly the wet samples of its positions, wrong: " + juce::String (wrong));
}

GF_TEST (Export, export_busy_rejected)
{
    ExportService svc;
    svc.prepare (kSr);
    svc.arm();
    std::int64_t pos = 0;
    feedRamp (svc, pos, 100);

    GF_EXPECT (svc.request ({ pos - 20000, 4800 }) == RequestResult::Accepted);
    GF_EXPECT_MSG (svc.request ({ pos - 20000, 4800 }) == RequestResult::Busy, "one request at a time");
    GF_EXPECT (requestText (RequestResult::Busy).isNotEmpty());

    juce::AudioBuffer<float> out;
    Failure why = Failure::None;
    GF_EXPECT (waitReady (svc, pos, 50, out, why));
    GF_EXPECT (svc.request ({ pos - 20000, 4800 }) == RequestResult::Accepted);

    // A request nobody has started copying can be cancelled; one in flight cannot.
    GF_EXPECT (svc.cancelPending());
    GF_EXPECT (svc.getState() == ExportState::Idle);
    GF_EXPECT (! svc.cancelPending());
}

GF_TEST (Export, export_samplerate_change_cancels)
{
    ExportService svc;
    svc.prepare (kSr);
    svc.arm();
    std::int64_t pos = 0;
    feedRamp (svc, pos, 100);
    GF_EXPECT (svc.request ({ pos - 20000, 4800 }) == RequestResult::Accepted);

    svc.prepare (96000.0);
    GF_EXPECT (svc.getState() == ExportState::Idle);
    GF_EXPECT (svc.getWriteEnd() == 0 && svc.getFirstPos() == -1);
    GF_EXPECT (svc.getRingSize() == (int) std::ceil (96000.0 * kRingSeconds));
    GF_EXPECT (svc.getSampleRate() == 96000.0);

    juce::AudioBuffer<float> out;
    Failure why = Failure::None;
    GF_EXPECT (! svc.take (out, why) && why == Failure::None);
}

GF_TEST (Export, export_wav_header_valid)
{
    juce::AudioBuffer<float> audio (2, 4800);

    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        audio.setSample (0, i, 0.5f * std::sin (0.05f * (float) i));
        audio.setSample (1, i, -0.25f * std::sin (0.03f * (float) i));
    }

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gapfill-export-test");
    dir.deleteRecursively();
    const auto file = dir.getChildFile ("out").getChildFile ("a.wav");

    juce::String error;
    GF_EXPECT_MSG (writeWav24 (file, audio, 44100.0, error), error);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
    GF_EXPECT (reader != nullptr);

    if (reader != nullptr)
    {
        GF_EXPECT (reader->numChannels == 2);
        GF_EXPECT (reader->sampleRate == 44100.0);
        GF_EXPECT (reader->bitsPerSample == 24);
        GF_EXPECT (reader->lengthInSamples == 4800);

        juce::AudioBuffer<float> back (2, 4800);
        reader->read (&back, 0, 4800, 0, true, true);
        GF_EXPECT (compareBuffers (audio, back, 1.0e-6).firstBad < 0);
    }

    // Errors are reported, not swallowed.
    juce::AudioBuffer<float> empty;
    GF_EXPECT (! writeWav24 (dir.getChildFile ("none.wav"), empty, 44100.0, error) && error.isNotEmpty());
    dir.deleteRecursively();
}

GF_TEST (Export, export_file_name_follows_the_spec)
{
    const auto when = juce::Time (2026, 9, 21, 10, 15, 30);          // month is 0-based: October
    GF_EXPECT (defaultFileName ("Pitch Rise", 120.4, when) == "GapFill_PitchRise_120bpm_20261021-101530.wav");
    GF_EXPECT (defaultFileName ("Stutter", 0.0, when) == "GapFill_Stutter_20261021-101530.wav");
    GF_EXPECT (defaultFileName ("a/b:c", 90.0, when).startsWith ("GapFill_abc_90bpm_"));
}

GF_TEST (Export, export_tap_does_not_change_the_output)
{
    const auto input = drumLoop (kSr, 150.0, 3);

    auto render = [&] (bool armed)
    {
        ProcRun run ({ { "fill", 90.0f }, { "density", 90.0f } });

        if (armed)
            run.proc.getRecipes().exportService.arm();

        juce::AudioBuffer<float> all (2, input.getNumSamples());
        all.clear();

        for (int i = 0; i * kBlock + kBlock <= input.getNumSamples(); ++i)
        {
            const auto& out = run.step (input);
            all.copyFrom (0, i * kBlock, out, 0, 0, kBlock);
            all.copyFrom (1, i * kBlock, out, 1, 0, kBlock);
        }

        return all;
    };

    const auto a = render (false), b = render (true);
    GF_EXPECT_MSG (compareBuffers (a, b, 0.0).firstBad < 0, "arming the tap leaves the audio bit-identical");
}

GF_TEST (Export, export_no_audio_thread_allocation)
{
    ProcRun run ({ { "fill", 80.0f }, { "density", 80.0f } });
    auto& svc = run.proc.getRecipes().exportService;
    svc.arm();
    const auto input = drumLoop (kSr, 150.0, 4);

    for (int i = 0; i < 100; ++i)
        run.step (input);

    run.proc.drainFillEvents();
    GF_EXPECT (run.proc.getRecipeHistory().size() > 0);
    const auto span = run.proc.getRecipeHistory().getSpan (0);

    long allocations = 0;
    bool requested = false;
    juce::AudioBuffer<float> out;
    Failure why = Failure::None;
    bool got = false;

    for (int i = 100; i < 260 && i * kBlock + kBlock <= input.getNumSamples(); ++i)
    {
        if (! requested && span.isKnown())
            requested = svc.request (span) == RequestResult::Accepted;

        {
            AllocGuard guard;
            run.step (input);
            allocations += guard.count();
        }

        if (! got)
            got = svc.take (out, why);                  // message-thread work, outside the guard
    }

    GF_EXPECT (requested && got);
    GF_EXPECT_MSG (allocations == 0, "audio thread allocated " + juce::String ((int) allocations) + " times with the tap armed and an export running");
}
