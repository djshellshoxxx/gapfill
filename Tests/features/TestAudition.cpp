// E-1 Fill Audition and U-3 MIDI note trigger
// (docs/dev/03-SPEC-ESSENTIALS.md E-1, docs/dev/05-SPEC-USABILITY-VALUE.md U-3).

#include "../support/AllocGuard.h"
#include "../support/RecipeRigs.h"
#include "../support/TestRig.h"

#include "dsp/FillEngine.h"
#include "features/audition/Audition.h"
#include "features/audition/MidiTrigger.h"
#include "features/audition/PreviewChannel.h"
#include "features/state/StateSubtrees.h"

using namespace gapfill;
using namespace gftest;

using namespace recipetest;

//==============================================================================
// E-1 engine behaviour

GF_TEST (Audition, audition_starts_without_gap)
{
    EngineRig rig;
    const auto input = steadyNoise();

    for (int i = 0; i < 40; ++i)
        rig.step (input, i);

    GF_EXPECT (! rig.engine.isPreviewActive());
    rig.engine.requestPreview (100);
    rig.step (input, 40);

    GF_EXPECT (rig.engine.isPreviewActive());
    GF_EXPECT (previewStatusOf (rig.engine.getPreviewStatusCode()) == PreviewStatus::Playing);
    GF_EXPECT (previewAlgoOf (rig.engine.getPreviewStatusCode()) >= 0);
    GF_EXPECT (rig.wetEnergy() > 0.0f);
    GF_EXPECT (rig.engine.getFillsStarted() == 0);
}

GF_TEST (Audition, audition_leaves_gap_counters_untouched)
{
    const auto input = drumLoop (kSr, 150.0, 4);

    auto make = [&] (EngineRig& rig)
    {
        rig.settings.fill01 = 0.8f;
        rig.settings.density01 = 0.8f;
    };

    // Two identical runs; one auditions in the middle. The gap bookkeeping, the fill count and the
    // reject counters must agree exactly, because a preview is not a gap.
    EngineRig withPreview, twin;
    make (withPreview);
    make (twin);

    for (int i = 0; i < 90; ++i)
    {
        if (i == 60)
            withPreview.engine.requestPreview (100);

        withPreview.step (input, i);
        twin.step (input, i);

        GF_EXPECT_MSG (withPreview.engine.getGapBookkeeping() == twin.engine.getGapBookkeeping(),
                       "bookkeeping diverged at block " + juce::String (i));
    }

    GF_EXPECT (withPreview.engine.getFillsStarted() == twin.engine.getFillsStarted());
    GF_EXPECT (twin.engine.getFillsStarted() > 0);

    for (int r = 0; r < numRejectReasons; ++r)
        GF_EXPECT (withPreview.engine.getRejectCount ((RejectReason) r) == twin.engine.getRejectCount ((RejectReason) r));
}

GF_TEST (Audition, audition_not_in_recipe_events)
{
    ProcRun run ({ { "fill", 0.0f } });
    const auto input = steadyNoise();

    for (int i = 0; i < 40; ++i)
        run.step (input);

    run.post (CommandType::Preview);
    run.step (input);
    run.step (input);

    GF_EXPECT (run.proc.drainFillEvents() >= 1);                  // the PreviewStarted event was queued
    GF_EXPECT (run.proc.getRecipeHistory().size() == 0);
    GF_EXPECT (run.proc.getGapOutcomeLog().size() == 0);
}

GF_TEST (Audition, audition_started_event_is_flagged_preview_and_carries_span)
{
    EngineRig rig;
    const auto input = steadyNoise();

    for (int i = 0; i < 40; ++i)
        rig.step (input, i);

    rig.engine.requestPreview (127);
    rig.step (input, 40);

    FillEvent e;
    GF_EXPECT (rig.events.pop (&e, 1) == 1);
    GF_EXPECT (e.type == FillEventType::Preview);
    GF_EXPECT ((e.recipe.flags & FillRecipe::flagPreview) != 0);
    GF_EXPECT (isValid (e.recipe));
    GF_EXPECT (e.lengthSamples > 0 && e.pos == rig.lastOutStart);
}

GF_TEST (Audition, audition_respects_wet_zero_and_output_trim)
{
    const auto input = steadyNoise();

    auto render = [&] (float wet, float outputDb, bool withPreview)
    {
        ProcRun run ({ { "fill", 0.0f }, { "dry", 0.0f }, { "wet", wet }, { "output", outputDb } });
        double energy = 0.0;

        for (int i = 0; i < 90; ++i)
        {
            if (i == 40 && withPreview)
                run.post (CommandType::Preview);

            const auto& out = run.step (input);

            if (i >= 40)
                energy += regionEnergy (out, 0, kBlock);
        }

        return energy;
    };

    GF_EXPECT_MSG (render (0.0f, 0.0f, true) < 1.0e-9, "wet = 0 silences the preview");

    const auto loud = render (100.0f, 0.0f, true);
    const auto quiet = render (100.0f, -12.0f, true);
    GF_EXPECT_MSG (loud > 1.0e-3, "the preview is audible through the wet path");
    GF_EXPECT_NEAR (quiet / loud, std::pow (10.0, -12.0 / 10.0), 0.02);      // energy ratio of -12 dB

    GF_EXPECT_MSG (render (100.0f, 0.0f, false) < 1.0e-9, "without a preview the wet-only output is silent (no natural fills)");
}

GF_TEST (Audition, audition_ducked_by_protection)
{
    const auto input = drumLoop (kSr, 150.0, 6);

    auto energyAroundHit = [&] (float prot)
    {
        EngineRig rig;
        rig.settings.protTrans01 = prot;
        rig.settings.protNext01 = prot;
        rig.settings.protectHoldMs = 120.0f;
        double energy = 0.0;
        bool requested = false;
        int after = -1;

        for (int i = 0; i < 400 && after < 4; ++i)
        {
            const auto lookahead = (std::int64_t) (rig.settings.lookaheadMs * 0.001 * kSr);
            const auto outStart = (std::int64_t) i * kBlock - lookahead;

            if (! requested && outStart > (std::int64_t) (kSr * 1.0)
                  && rig.analysis.nextMainEvent (outStart) >= outStart
                  && rig.analysis.nextMainEvent (outStart) < outStart + kBlock)
            {
                rig.engine.requestPreview (100);
                requested = true;
                after = 0;
            }

            rig.step (input, i);

            if (after >= 0)
            {
                energy += rig.wetEnergy();
                ++after;
            }
        }

        GF_EXPECT_MSG (requested, "found a hit to audition over");
        return energy;
    };

    const auto open = energyAroundHit (0.0f);
    const auto guarded = energyAroundHit (1.0f);
    GF_EXPECT (open > 0.0);
    GF_EXPECT_MSG (guarded < 0.6 * open, "a preview played over a hit is ducked: " + juce::String (guarded) + " vs " + juce::String (open));
}

GF_TEST (Audition, audition_rejects_no_source)
{
    EngineRig rig;
    const auto silence = makeSilence (2, (int) (kSr * 2.0));

    for (int i = 0; i < 40; ++i)
        rig.step (silence, i);

    rig.engine.requestPreview (100);
    rig.step (silence, 40);

    GF_EXPECT (! rig.engine.isPreviewActive());
    GF_EXPECT (previewStatusOf (rig.engine.getPreviewStatusCode()) == PreviewStatus::RejectedNoSource);
    GF_EXPECT (std::string (previewRejectionText (PreviewStatus::RejectedNoSource)) == "Nothing to sample - play some audio");
}

GF_TEST (Audition, audition_rejects_no_band)
{
    EngineRig rig;
    rig.settings.mode = DetMode::Spectral;

    for (auto& on : rig.settings.bandOn)
        on = false;

    const auto input = steadyNoise();

    for (int i = 0; i < 40; ++i)
        rig.step (input, i);

    rig.engine.requestPreview (100);
    rig.step (input, 40);
    GF_EXPECT (previewStatusOf (rig.engine.getPreviewStatusCode()) == PreviewStatus::RejectedNoBand);
    GF_EXPECT (! rig.engine.isPreviewActive());

    rig.settings.bandOn[1] = true;
    rig.engine.requestPreview (100);
    rig.step (input, 41);
    GF_EXPECT (previewStatusOf (rig.engine.getPreviewStatusCode()) == PreviewStatus::Playing);
}

GF_TEST (Audition, audition_restart_fades_old_voice)
{
    EngineRig rig;
    const auto input = steadyNoise();

    for (int i = 0; i < 40; ++i)
        rig.step (input, i);

    rig.engine.requestPreview (100);

    std::vector<float> trace;       // left channel, concatenated

    auto collect = [&]
    {
        for (int i = 0; i < kBlock; ++i)
            trace.push_back (rig.wet.getSample (0, i));
    };

    for (int i = 40; i < 44; ++i)
    {
        rig.step (input, i);
        collect();
    }

    const auto boundary = (int) trace.size();
    rig.engine.requestPreview (100);        // restart while the first one plays
    rig.step (input, 44);
    collect();
    rig.step (input, 45);
    collect();

    GF_EXPECT (rig.engine.isPreviewActive());
    GF_EXPECT (allFinite (rig.wet));

    // No step at the restart boundary beyond what the material itself does.
    float worstBefore = 1.0e-4f;

    for (int i = boundary - 1500; i < boundary - 1; ++i)
        worstBefore = juce::jmax (worstBefore, std::abs (trace[(size_t) i + 1] - trace[(size_t) i]));

    float atBoundary = 0.0f;

    for (int i = boundary - 2; i < boundary + 300; ++i)
        atBoundary = juce::jmax (atBoundary, std::abs (trace[(size_t) i + 1] - trace[(size_t) i]));

    GF_EXPECT_MSG (atBoundary < 4.0f * worstBefore + 0.02f,
                   "restart jump " + juce::String (atBoundary) + " vs material " + juce::String (worstBefore));
}

GF_TEST (Audition, audition_coalesces_pending)
{
    ProcRun run ({ { "fill", 0.0f } });
    const auto input = steadyNoise();

    for (int i = 0; i < 40; ++i)
        run.step (input);

    run.proc.drainFillEvents();
    run.post (CommandType::Preview);
    run.post (CommandType::Preview);
    run.post (CommandType::Preview);
    run.step (input);

    GF_EXPECT_MSG (run.proc.drainFillEvents() == 1, "three presses in one block start one preview");
}

GF_TEST (Audition, audition_stale_command_discarded)
{
    FillEngine engine;
    engine.prepare (kSr, kBlock, 2);

    Command c;
    c.type = CommandType::Preview;
    c.velocity = 100;
    c.issuedMs = 1000;

    GF_EXPECT (audition::handleCommand (engine, c, 1000 + kPreviewStaleMs));          // exactly at the limit is honoured
    GF_EXPECT (engine.isPreviewPending());

    engine.reset();
    GF_EXPECT (! engine.isPreviewPending());

    GF_EXPECT (audition::handleCommand (engine, c, 1000 + kPreviewStaleMs + 1));      // handled, but dropped as stale
    GF_EXPECT_MSG (! engine.isPreviewPending(), "a command older than 500 ms never fires");

    c.issuedMs = 0;                                                                   // unstamped (tests, MIDI)
    GF_EXPECT (audition::handleCommand (engine, c, 99999999u));
    GF_EXPECT (engine.isPreviewPending());

    Command other;
    other.type = CommandType::LikeLatest;
    GF_EXPECT (! audition::handleCommand (engine, other));
    GF_EXPECT (isStalePreviewCommand (5, 506) && ! isStalePreviewCommand (5, 505) && ! isStalePreviewCommand (0, 99999));
}

GF_TEST (Audition, audition_ignored_when_bypassed)
{
    ProcRun run ({ { "fill", 0.0f }, { "bypass", 1.0f } });
    const auto input = steadyNoise();

    for (int i = 0; i < 40; ++i)
        run.step (input);

    run.post (CommandType::Preview);
    run.step (input);
    run.step (input);

    GF_EXPECT (! run.proc.getFillEngine().isPreviewActive());
    GF_EXPECT_MSG (run.status() == PreviewStatus::RejectedBypassed, "bypassed => status Bypassed");
    GF_EXPECT (run.proc.drainFillEvents() == 0);
}

GF_TEST (Audition, audition_fades_on_panic_reset_samplerate_change)
{
    auto maxJump = [] (const juce::AudioBuffer<float>& b) { return maxSampleJump (b); };

    // panic: fades over ~4 ms and ends
    {
        EngineRig rig;
        const auto input = steadyNoise();

        for (int i = 0; i < 40; ++i)
            rig.step (input, i);

        rig.engine.requestPreview (100);
        rig.step (input, 40);
        rig.step (input, 41);
        GF_EXPECT (rig.engine.isPreviewActive());

        rig.engine.panic();
        rig.step (input, 42);
        GF_EXPECT (! rig.engine.isPreviewActive());
        GF_EXPECT (allFinite (rig.wet));
        GF_EXPECT_MSG (maxJump (rig.wet) < 0.25f, "panic fades instead of cutting");

        const auto tail = regionEnergy (rig.wet, 400, 112);
        GF_EXPECT (tail < 1.0e-9f);
    }

    // reset: the preview is faded, never hard-cut, and the sequence restarts
    {
        EngineRig rig;
        const auto input = steadyNoise();

        for (int i = 0; i < 40; ++i)
            rig.step (input, i);

        rig.engine.requestPreview (100);
        rig.step (input, 40);
        rig.engine.reset();
        GF_EXPECT_MSG (rig.engine.isPreviewActive(), "reset starts a fade, it does not zero the voice");
        rig.step (input, 41);
        GF_EXPECT (! rig.engine.isPreviewActive());
        GF_EXPECT (maxJump (rig.wet) < 0.25f);
    }

    // sample-rate change: buffers are reallocated, nothing keeps playing or stays pending
    {
        EngineRig rig;
        const auto input = steadyNoise();

        for (int i = 0; i < 40; ++i)
            rig.step (input, i);

        rig.engine.requestPreview (100);
        rig.step (input, 40);
        rig.engine.prepare (96000.0, kBlock, 2);
        GF_EXPECT (! rig.engine.isPreviewActive() && ! rig.engine.isPreviewPending());
        GF_EXPECT (previewStatusOf (rig.engine.getPreviewStatusCode()) == PreviewStatus::Idle);
    }
}

GF_TEST (Audition, audition_deterministic_sequence_for_same_seed)
{
    const auto input = steadyNoise();

    auto sequence = [&] (int seed)
    {
        EngineRig rig;
        rig.settings.seed = seed;
        juce::AudioBuffer<float> all (2, kBlock * 30);
        all.clear();

        for (int i = 0; i < 36; ++i)
            rig.step (input, i);

        for (int press = 0; press < 3; ++press)
        {
            rig.engine.requestPreview (100);

            for (int i = 0; i < 10; ++i)
            {
                rig.step (input, 36 + press * 10 + i);
                all.copyFrom (0, (press * 10 + i) * kBlock, rig.wet, 0, 0, kBlock);
                all.copyFrom (1, (press * 10 + i) * kBlock, rig.wet, 1, 0, kBlock);
            }
        }

        return all;
    };

    const auto a = sequence (7), b = sequence (7), c = sequence (8);
    GF_EXPECT_MSG (compareBuffers (a, b, 0.0).firstBad < 0, "same seed + same presses => bit-identical previews");
    GF_EXPECT_MSG (compareBuffers (a, c, 0.0).firstBad >= 0, "a different seed gives different previews");
    GF_EXPECT (regionEnergy (a, 0, a.getNumSamples()) > 0.0f);
}

GF_TEST (Audition, audition_does_not_change_next_natural_fill)
{
    const auto input = drumLoop (kSr, 150.0, 8);

    struct Result { std::vector<FillRecipe> recipes; juce::AudioBuffer<float> out; int fills = 0; };

    auto render = [&] (bool withPreview)
    {
        ProcRun run ({ { "fill", 80.0f }, { "density", 90.0f } });
        Result r;
        r.out.setSize (2, input.getNumSamples());
        r.out.clear();

        for (int i = 0; i * kBlock + kBlock <= input.getNumSamples(); ++i)
        {
            if (withPreview && i == 20)
                run.post (CommandType::Preview);

            const auto& out = run.step (input);
            r.out.copyFrom (0, i * kBlock, out, 0, 0, kBlock);
            r.out.copyFrom (1, i * kBlock, out, 1, 0, kBlock);
        }

        run.proc.drainFillEvents();
        const auto& h = run.proc.getRecipeHistory();

        for (int i = 0; i < h.size(); ++i)
            r.recipes.push_back (h.get (i));

        r.fills = run.proc.getFillEngine().getFillsStarted();
        return r;
    };

    const auto plain = render (false);
    const auto with = render (true);

    GF_EXPECT (plain.fills > 2);
    GF_EXPECT (plain.fills == with.fills);
    GF_EXPECT (plain.recipes.size() == with.recipes.size());

    for (size_t i = 0; i < plain.recipes.size() && i < with.recipes.size(); ++i)
        GF_EXPECT_MSG (std::memcmp (&plain.recipes[i], &with.recipes[i], sizeof (FillRecipe)) == 0,
                       "natural recipe " + juce::String ((int) i) + " is unchanged by an audition");

    // After the preview (<= 2 s + tail) the two renders must agree sample for sample.
    const auto from = 20 * kBlock + (int) (kSr * 2.3);
    juce::AudioBuffer<float> a (2, plain.out.getNumSamples() - from), b (2, a.getNumSamples());

    for (int ch = 0; ch < 2; ++ch)
    {
        a.copyFrom (ch, 0, plain.out, ch, from, a.getNumSamples());
        b.copyFrom (ch, 0, with.out, ch, from, b.getNumSamples());
    }

    const auto diff = compareBuffers (a, b, 1.0e-6);
    GF_EXPECT_MSG (diff.firstBad < 0, "output after the preview is unchanged, max diff " + juce::String (diff.maxAbs));
}

GF_TEST (Audition, audition_no_audio_thread_allocation)
{
    ProcRun run ({ { "fill", 60.0f } });
    const auto input = drumLoop (kSr, 150.0, 4);

    for (int i = 0; i < 100; ++i)
        run.step (input);                            // warm up JUCE internals

    long allocations = 0;

    for (int i = 100; i < 160; ++i)
    {
        if (i % 7 == 0)
            run.post (CommandType::Preview);

        if (i % 11 == 0)
            run.midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

        AllocGuard guard;
        run.step (input);
        allocations += guard.count();
    }

    GF_EXPECT_MSG (allocations == 0, "audio thread allocated " + juce::String ((int) allocations) + " times with previews running");
}

GF_TEST (Audition, audition_midi_cc_edge_and_rearm)
{
    audition::MidiTrigger t;
    t.setCC (20);

    auto cc = [&] (int value, int channel = 1)
    {
        juce::MidiBuffer b;
        b.addEvent (juce::MidiMessage::controllerEvent (channel, 20, value), 0);
        std::uint8_t vel = 0;
        return t.scan (b, vel);
    };

    GF_EXPECT (! cc (10));
    GF_EXPECT (! cc (63));
    GF_EXPECT_MSG (cc (64), "rising edge at 64 fires");
    GF_EXPECT_MSG (! cc (127), "stays high: no repeat");
    GF_EXPECT (! cc (60));
    GF_EXPECT_MSG (! cc (100), "between 48 and 64 it has not re-armed");
    GF_EXPECT (! cc (47));
    GF_EXPECT_MSG (cc (90), "re-armed below 48");

    t.stopLearn();
    t.setCC (-1);
    GF_EXPECT (! cc (127));

    // Learn: the next CC is taken, and it must fall before it can fire.
    t.startLearn (audition::MidiTrigger::Learn::CC);
    GF_EXPECT (! cc (127));
    GF_EXPECT (t.getCC() == 20 && t.getLearn() == audition::MidiTrigger::Learn::None);
    GF_EXPECT (! cc (127));
    GF_EXPECT (! cc (0));
    GF_EXPECT (cc (127));
}

GF_TEST (Audition, audition_finite_output_all_algorithms)
{
    const auto input = steadyNoise();

    for (int algo = 0; algo < numAlgos; ++algo)
    {
        EngineRig rig;
        rig.settings.fillTypeIndex = algo + 1;
        rig.settings.complexity01 = 0.9f;

        for (int i = 0; i < 40; ++i)
            rig.step (input, i);

        rig.engine.requestPreview (127);
        float energy = 0.0f;
        bool finite = true;

        for (int i = 40; i < 90; ++i)
        {
            rig.step (input, i);
            finite = finite && allFinite (rig.wet);
            energy += rig.wetEnergy();
        }

        GF_EXPECT_MSG (finite, algoName ((Algo) algo));
        GF_EXPECT_MSG (energy > 0.0f, juce::String (algoName ((Algo) algo)) + " produced audio");
        GF_EXPECT_MSG (! rig.engine.isPreviewActive(), juce::String (algoName ((Algo) algo)) + " finished");
    }
}

GF_TEST (Audition, audition_memory_budget_under_one_megabyte_at_192k)
{
    FillEngine engine;
    engine.prepare (192000.0, 512, 2);
    const auto bytes = engine.getPreviewBufferBytes();

    std::cout << "         sizeof(FillVoice) = " << sizeof (FillVoice) << " B, sizeof(PreviewChannel) = " << sizeof (PreviewChannel)
              << " B, preview buffers at 192 kHz stereo = " << bytes << " B" << std::endl;

    GF_EXPECT_MSG (bytes < 1024u * 1024u, "the extra voice stays under 1 MB at 192 kHz stereo: " + juce::String ((int) bytes));
    GF_EXPECT (bytes > 100000u);
}

GF_TEST (Audition, audition_respects_fill_type_and_caps_length)
{
    EngineRig rig;
    rig.settings.fillTypeIndex = (int) Algo::Stutter + 1;
    rig.settings.maxFillMs = 1500.0f;
    const auto input = steadyNoise();

    for (int i = 0; i < 40; ++i)
        rig.step (input, i);

    rig.engine.requestPreview (100);
    rig.step (input, 40);
    GF_EXPECT (previewAlgoOf (rig.engine.getPreviewStatusCode()) == (int) Algo::Stutter);

    FillEvent e;
    GF_EXPECT (rig.events.pop (&e, 1) == 1);
    GF_EXPECT_MSG (e.lengthSamples <= (int) (kSr * kPreviewMaxSeconds), "capped at 2000 ms");
}

//==============================================================================
// U-3 MIDI note trigger

namespace
{
    int previewsFrom (ProcRun& run, const juce::AudioBuffer<float>& input, int blocks,
                      std::function<void (int, juce::MidiBuffer&)> midiAt)
    {
        int total = 0;

        for (int i = 0; i < blocks; ++i)
        {
            midiAt (i, run.midi);
            run.step (input);
            total += run.proc.drainFillEvents();
        }

        return total;
    }
}

GF_TEST (NoteTrigger, note_trigger_fires_once_per_noteon)
{
    ProcRun run ({ { "fill", 0.0f } });
    run.proc.getRecipes().trigger.setNote (60);
    const auto input = steadyNoise();

    previewsFrom (run, input, 40, [] (int, juce::MidiBuffer&) {});

    const auto n = previewsFrom (run, input, 30, [] (int i, juce::MidiBuffer& m)
    {
        if (i == 2 || i == 3 || i == 15)
            m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

        if (i == 8)
            m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);       // NoteOff is ignored
    });

    GF_EXPECT_MSG (n == 3, "three NoteOns, three previews (got " + juce::String (n) + ")");
}

GF_TEST (NoteTrigger, wrong_note_or_channel_ignored)
{
    ProcRun run ({ { "fill", 0.0f } });
    auto& t = run.proc.getRecipes().trigger;
    t.setNote (60);
    t.setChannel (3);
    const auto input = steadyNoise();

    previewsFrom (run, input, 40, [] (int, juce::MidiBuffer&) {});

    const auto n = previewsFrom (run, input, 20, [] (int i, juce::MidiBuffer& m)
    {
        if (i == 1) m.addEvent (juce::MidiMessage::noteOn (3, 61, (juce::uint8) 100), 0);     // wrong note
        if (i == 5) m.addEvent (juce::MidiMessage::noteOn (4, 60, (juce::uint8) 100), 0);     // wrong channel
        if (i == 8) m.addEvent (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 0), 0);       // velocity 0 = NoteOff
    });

    GF_EXPECT (n == 0);

    const auto m = previewsFrom (run, input, 5, [] (int i, juce::MidiBuffer& mb)
    {
        if (i == 1) mb.addEvent (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 100), 0);
    });

    GF_EXPECT (m == 1);

    t.setChannel (0);                // any channel
    const auto any = previewsFrom (run, input, 5, [] (int i, juce::MidiBuffer& mb)
    {
        if (i == 1) mb.addEvent (juce::MidiMessage::noteOn (9, 60, (juce::uint8) 100), 0);
    });

    GF_EXPECT (any == 1);
}

GF_TEST (NoteTrigger, velocity_scales_amplitude)
{
    const auto input = steadyNoise();

    auto energyFor = [&] (std::uint8_t velocity)
    {
        ProcRun run ({ { "fill", 0.0f }, { "dry", 0.0f }, { "wet", 100.0f } });
        run.proc.getRecipes().trigger.setNote (60);
        double energy = 0.0;

        for (int i = 0; i < 100; ++i)
        {
            if (i == 40)
                run.midi.addEvent (juce::MidiMessage::noteOn (1, 60, velocity), 0);

            const auto& out = run.step (input);

            if (i >= 40)
                energy += regionEnergy (out, 0, kBlock);
        }

        return energy;
    };

    const auto loud = energyFor (127), soft = energyFor (1);
    GF_EXPECT (loud > 1.0e-3);
    // amplitude lerp (0.5, 1.0, vel / 127): vel 1 is about 0.504, so energy about 0.254 of vel 127
    GF_EXPECT_NEAR (soft / loud, 0.504 * 0.504, 0.03);
}

GF_TEST (NoteTrigger, learn_times_out)
{
    audition::MidiTrigger t;
    t.startLearn (audition::MidiTrigger::Learn::Note);
    GF_EXPECT (t.getLearn() == audition::MidiTrigger::Learn::Note);

    int ticks = 0;
    bool expired = false;

    while (! expired && ticks < 1000)
    {
        expired = t.advanceLearnTimeout();
        ++ticks;
    }

    GF_EXPECT_MSG (ticks == audition::MidiTrigger::learnTicks, "10 s at 15 Hz = " + juce::String (ticks) + " ticks");
    GF_EXPECT (t.getLearn() == audition::MidiTrigger::Learn::None && t.getNote() == -1);
    GF_EXPECT (! t.advanceLearnTimeout());

    // A NoteOn within the window becomes the trigger, on any channel.
    t.startLearn (audition::MidiTrigger::Learn::Note);
    juce::MidiBuffer b;
    b.addEvent (juce::MidiMessage::noteOn (5, 72, (juce::uint8) 90), 0);
    std::uint8_t vel = 0;
    GF_EXPECT_MSG (! t.scan (b, vel), "the learning NoteOn itself does not fire");
    GF_EXPECT (t.getNote() == 72 && t.getLearn() == audition::MidiTrigger::Learn::None);
}

GF_TEST (NoteTrigger, trigger_roundtrip_state)
{
    GapFillAudioProcessor a;
    a.getRecipes().trigger.setNote (36);
    a.getRecipes().trigger.setChannel (10);
    a.getRecipes().trigger.setCC (21);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    GapFillAudioProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    GF_EXPECT (b.getRecipes().trigger.getNote() == 36);
    GF_EXPECT (b.getRecipes().trigger.getChannel() == 10);
    GF_EXPECT (b.getRecipes().trigger.getCC() == 21);

    // Defaults write no <Perf> subtree at all.
    GapFillAudioProcessor c;
    juce::MemoryBlock plain;
    c.getStateInformation (plain);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (plain.getData(), (int) plain.getSize());
    GF_EXPECT (xml != nullptr && xml->getChildByName ("Perf") == nullptr);

    // A state without the subtree resets the trigger.
    b.setStateInformation (plain.getData(), (int) plain.getSize());
    GF_EXPECT (b.getRecipes().trigger.getNote() == -1 && b.getRecipes().trigger.getCC() == -1);
}

GF_TEST (NoteTrigger, trigger_state_rejects_out_of_range_but_keeps_the_rest)
{
    juce::ValueTree bad ("Perf");
    bad.setProperty ("trigNote", 200, nullptr);
    GF_EXPECT (! audition::MidiTrigger::isValid (bad));

    juce::ValueTree text ("Perf");
    text.setProperty ("trigChan", "abc", nullptr);
    GF_EXPECT (! audition::MidiTrigger::isValid (text));

    juce::ValueTree good ("Perf");
    good.setProperty ("trigNote", 127, nullptr);
    good.setProperty ("trigChan", "16", nullptr);
    GF_EXPECT (audition::MidiTrigger::isValid (good));

    // End to end: a corrupt <Perf> is dropped while the parameters still load.
    GapFillAudioProcessor src;
    setParams (src, { { "fill", 77.0f } });
    src.getRecipes().trigger.setNote (40);
    juce::MemoryBlock blob;
    src.getStateInformation (blob);

    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    GF_EXPECT (xml != nullptr);

    if (auto* perf = xml->getChildByName ("Perf"))
        perf->setAttribute ("trigNote", 999);

    juce::MemoryBlock corrupted;
    juce::AudioProcessor::copyXmlToBinary (*xml, corrupted);

    GapFillAudioProcessor dst;
    dst.getRecipes().trigger.setNote (12);
    dst.setStateInformation (corrupted.getData(), (int) corrupted.getSize());
    GF_EXPECT_MSG (dst.getRecipes().trigger.getNote() == -1, "the corrupt subtree was dropped and reset");
    GF_EXPECT_NEAR (dst.getValueTreeState().getRawParameterValue ("fill")->load(), 77.0, 0.01);
}

GF_TEST (NoteTrigger, note_ignored_when_bypassed)
{
    ProcRun run ({ { "fill", 0.0f }, { "bypass", 1.0f } });
    run.proc.getRecipes().trigger.setNote (60);
    const auto input = steadyNoise();

    previewsFrom (run, input, 40, [] (int, juce::MidiBuffer&) {});
    const auto n = previewsFrom (run, input, 10, [] (int i, juce::MidiBuffer& m)
    {
        if (i == 2) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    });

    GF_EXPECT (n == 0);
    GF_EXPECT (run.status() == PreviewStatus::RejectedBypassed);
}

GF_TEST (NoteTrigger, coalesces_with_ui_request)
{
    ProcRun run ({ { "fill", 0.0f } });
    run.proc.getRecipes().trigger.setNote (60);
    const auto input = steadyNoise();

    previewsFrom (run, input, 40, [] (int, juce::MidiBuffer&) {});
    run.post (CommandType::Preview);
    const auto n = previewsFrom (run, input, 3, [] (int i, juce::MidiBuffer& m)
    {
        if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    });

    GF_EXPECT_MSG (n == 1, "a UI press and a NoteOn in one block start one preview");
}

GF_TEST (NoteTrigger, no_midi_is_produced)
{
    GapFillAudioProcessor proc;
    GF_EXPECT (proc.acceptsMidi() && ! proc.producesMidi());
}
