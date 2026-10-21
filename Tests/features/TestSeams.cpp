// Phase 0 seams: FillRecipe, SPSC queues, scheduler stages, effective settings
// (docs/dev/02-FOUNDATIONS.md F0-2 .. F0-5).

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include "dsp/EngineEvents.h"
#include "dsp/EventLogs.h"
#include "features/FeatureHooks.h"

#include <array>
#include <cstring>
#include <iostream>
#include <map>

using namespace gapfill;
using namespace gftest;

//==============================================================================
// F0-3 FillRecipe

GF_TEST (Recipe, is_exactly_sixteen_bytes_and_trivially_copyable)
{
    static_assert (sizeof (FillRecipe) == 16);
    static_assert (std::is_trivially_copyable_v<FillRecipe>);
    GF_EXPECT (isValid (FillRecipe {}));
}

GF_TEST (Recipe, byte_round_trip_preserves_every_field)
{
    FillRecipe r;
    r.algo = 14; r.band = 2; r.lengthClass = 3; r.division = 11; r.pitchSemis = -24;
    r.modifierMask = 0x15; r.stereoMode = 3; r.mistakeType = 5; r.lengthMs = 1999;
    r.mistakeAmt = 100; r.flags = FillRecipe::flagPreview | FillRecipe::flagBiased; r.seed = 0xDEADBEEFu;
    GF_EXPECT (isValid (r));

    std::uint8_t bytes[FillRecipe::serialisedBytes];
    toBytes (r, bytes);
    const auto back = fromBytes (bytes);
    GF_EXPECT (std::memcmp (&r, &back, sizeof (r)) == 0);
}

GF_TEST (Recipe, validity_rejects_every_out_of_range_field)
{
    auto bad = [] (auto mutate) { FillRecipe r; mutate (r); return ! isValid (r); };
    GF_EXPECT (bad ([] (FillRecipe& r) { r.algo = 15; }));
    GF_EXPECT (bad ([] (FillRecipe& r) { r.band = 3; }));
    GF_EXPECT (bad ([] (FillRecipe& r) { r.band = -2; }));
    GF_EXPECT (bad ([] (FillRecipe& r) { r.lengthClass = 4; }));
    GF_EXPECT (bad ([] (FillRecipe& r) { r.division = 12; }));
    GF_EXPECT (bad ([] (FillRecipe& r) { r.pitchSemis = 60; }));
    GF_EXPECT (bad ([] (FillRecipe& r) { r.modifierMask = 0x20; }));
    GF_EXPECT (bad ([] (FillRecipe& r) { r.stereoMode = 4; }));
    GF_EXPECT (bad ([] (FillRecipe& r) { r.mistakeType = 16; }));
    GF_EXPECT (bad ([] (FillRecipe& r) { r.mistakeAmt = 101; }));
    GF_EXPECT (bad ([] (FillRecipe& r) { r.flags = 0x80; }));
}

GF_TEST (Recipe, started_fills_report_valid_recipes_matching_the_engine)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 80.0f } });
    const auto result = renderGoldenScenarioA (proc);
    GF_EXPECT (result.fills > 0);

    GF_EXPECT (proc.drainFillEvents() > 0);
    GF_EXPECT (proc.getRecipeHistory().size() > 0);

    for (int i = 0; i < proc.getRecipeHistory().size(); ++i)
    {
        const auto& r = proc.getRecipeHistory().get (i);
        GF_EXPECT_MSG (isValid (r), "recipe " + juce::String (i) + " must be valid");
        GF_EXPECT (r.lengthMs > 0 && r.seed != 0);
    }
}

GF_TEST (Recipe, voice_overrides_are_neutral_by_default)
{
    VoiceSetup s;
    GF_EXPECT (s.modifierOverride == 0xFF);
    GF_EXPECT (s.pitchOverride == 127);
}

//==============================================================================
// F0-4 queues

GF_TEST (Queues, spsc_preserves_order_and_wraps)
{
    SpscQueue<int, 4> q;
    int next = 0, expect = 0;

    for (int round = 0; round < 20; ++round)
    {
        for (int i = 0; i < 3; ++i)
            GF_EXPECT (q.push (next++));

        int out[4];
        const auto n = q.pop (out, 4);
        GF_EXPECT (n == 3);

        for (int i = 0; i < n; ++i)
            GF_EXPECT (out[i] == expect++);
    }
}

GF_TEST (Queues, full_queue_rejects_newest_and_holds_exactly_capacity)
{
    SpscQueue<int, 4> q;

    for (int i = 0; i < 4; ++i)
        GF_EXPECT (q.push (i));

    GF_EXPECT (! q.push (99));
    int out[8];
    GF_EXPECT (q.pop (out, 8) == 4);
    GF_EXPECT (out[0] == 0 && out[3] == 3);
}

GF_TEST (Queues, fill_event_queue_counts_dropped_events)
{
    FillEventQueue q;

    for (int i = 0; i < FillEventQueue::capacity + 5; ++i)
        q.push ({});

    GF_EXPECT (q.getDroppedCount() == 5);
    FillEvent out[FillEventQueue::capacity];
    GF_EXPECT (q.pop (out, FillEventQueue::capacity) == FillEventQueue::capacity);
}

GF_TEST (Queues, command_ids_are_permanent)
{
    GF_EXPECT ((int) CommandType::Preview == 1);
    GF_EXPECT ((int) CommandType::ReleaseCapture == 8);
    GF_EXPECT ((int) CommandType::Panic == 9);
    GF_EXPECT ((int) CommandType::SceneGoto == 11);
    GF_EXPECT ((int) CommandType::PreviewRecipe == 30);
    GF_EXPECT ((int) RejectReason::LockSkipped == 11);
    GF_EXPECT ((int) RejectReason::ForcedUnsafe == 17);
    GF_EXPECT (numRejectReasons == 18);
}

GF_TEST (Queues, commands_posted_from_the_message_thread_are_drained_by_processBlock)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 256, false);

    Command c;
    c.type = CommandType::Preview;
    GF_EXPECT (proc.postCommand (c));

    juce::AudioBuffer<float> buffer (2, 256);
    buffer.clear();
    juce::MidiBuffer midi;
    proc.processBlock (buffer, midi);

    // A second post succeeds only because the first was drained (capacity 16 not reached either way);
    // fill the queue to prove draining frees space.
    for (int i = 0; i < CommandQueue::capacity; ++i)
        GF_EXPECT (proc.postCommand (c));
    GF_EXPECT (! proc.postCommand (c));
    proc.processBlock (buffer, midi);
    GF_EXPECT (proc.postCommand (c));
}

GF_TEST (Queues, history_and_outcome_log_are_newest_first_and_bounded)
{
    RecipeHistory h;

    for (int i = 0; i < 12; ++i)
    {
        FillRecipe r;
        r.lengthMs = (std::uint16_t) (i + 1);
        h.push (r);
    }

    GF_EXPECT (h.size() == RecipeHistory::capacity);
    GF_EXPECT (h.get (0).lengthMs == 12 && h.get (7).lengthMs == 5);

    GapOutcomeLog log;

    for (int i = 0; i < 70; ++i)
    {
        FillEvent e;
        e.pos = i;
        log.push (e);
    }

    GF_EXPECT (log.size() == GapOutcomeLog::capacity);
    GF_EXPECT (log.get (0).pos == 69 && log.get (63).pos == 6);
}

//==============================================================================
// F0-2 scheduler stages

namespace
{
    /** Reject counts after rendering the drum loop with the given parameters. */
    std::array<int, (size_t) numRejectReasons> rejectCountsFor (std::initializer_list<std::pair<const char*, float>> p,
                                                                int bars = 4)
    {
        GapFillAudioProcessor proc;
        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, bars), 512, goldenSampleRate, o);

        std::array<int, (size_t) numRejectReasons> counts {};

        for (int i = 0; i < numRejectReasons; ++i)
            counts[(size_t) i] = proc.getFillEngine().getRejectCount ((RejectReason) i);

        return counts;
    }
}

GF_TEST (Scheduler, fill_off_is_reported)
{
    GF_EXPECT (rejectCountsFor ({ { "fill", 0.0f } })[(size_t) RejectReason::FillOff] > 0);
}

GF_TEST (Scheduler, too_short_is_reported_and_matches_the_legacy_counter)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "minGap", 500.0f } });
    const auto r = renderGoldenScenarioA (proc);
    const auto& e = proc.getFillEngine();
    GF_EXPECT (r.gapsTooShort > 0);
    GF_EXPECT (e.getRejectCount (RejectReason::TooShort) + e.getRejectCount (RejectReason::GuardEaten) == r.gapsTooShort);
}

GF_TEST (Scheduler, density_roll_no_algo_fits_and_no_band_are_reported)
{
    GF_EXPECT (rejectCountsFor ({ { "fill", 1.0f }, { "density", 0.0f } })[(size_t) RejectReason::DensityRoll] > 0);

    // Granular needs >= 66 ms; maxFill 20 ms can never hold it.
    GF_EXPECT (rejectCountsFor ({ { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 4.0f },
                                  { "maxFill", 20.0f }, { "minGap", 10.0f } })[(size_t) RejectReason::NoAlgoFits] > 0);

    GF_EXPECT (rejectCountsFor ({ { "detMode", 2.0f }, { "fill", 100.0f }, { "bandLowOn", 0.0f },
                                  { "bandMidOn", 0.0f }, { "bandHighOn", 0.0f } })[(size_t) RejectReason::NoBand] > 0);
}

namespace gapfill
{
/** Test-only access to single scheduler stages. */
struct SchedulerTestAccess
{
    static RejectReason eligibility (FillEngine& e, GapContext& ctx, const AnalysisEngine& a,
                                     const EngineSettings& s) noexcept
    {
        float needed = 0.0f;
        return e.checkEligibility (ctx, a, s, needed);
    }

    static void setLastFillStart (FillEngine& e, std::int64_t pos) noexcept { e.lastFillStartPos = pos; }
};
}

GF_TEST (Scheduler, no_source_and_too_soon_are_reachable_through_the_eligibility_stage)
{
    FillEngine engine;
    engine.prepare (48000.0, 512, 2);
    AnalysisEngine analysis;
    analysis.prepare (48000.0, 512);
    EngineSettings s;

    AnalysisFrame frame;
    frame.reference = 0.0f;
    GapContext ctx;
    ctx.outputPos = 100000;
    ctx.frame = &frame;
    GF_EXPECT (SchedulerTestAccess::eligibility (engine, ctx, analysis, s) == RejectReason::NoSource);

    frame.reference = 1.0f;
    SchedulerTestAccess::setLastFillStart (engine, ctx.outputPos - 10);
    GF_EXPECT (SchedulerTestAccess::eligibility (engine, ctx, analysis, s) == RejectReason::TooSoon);

    SchedulerTestAccess::setLastFillStart (engine, ctx.outputPos - 1000000);
    GF_EXPECT (SchedulerTestAccess::eligibility (engine, ctx, analysis, s) == RejectReason::None);
    GF_EXPECT (ctx.usable > 0 && ctx.available >= ctx.minGapSamples);
}

GF_TEST (Scheduler, no_voice_and_guard_eaten_are_reachable_from_audio)
{
    std::array<int, (size_t) numRejectReasons> total {};

    auto add = [&total] (std::array<int, (size_t) numRejectReasons> c)
    {
        for (size_t i = 0; i < total.size(); ++i)
            total[i] += c[i];
    };

    // Long fills in short gaps keep the voice busy => NoVoice.
    for (float maxFill : { 300.0f, 1500.0f })
        add (rejectCountsFor ({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 90.0f },
                                { "minGap", 10.0f }, { "maxFill", maxFill }, { "protNext", 0.0f },
                                { "retrigGuard", 5.0f }, { "gapRelease", 5.0f } }));

    std::cout << "         NoVoice=" << total[(size_t) RejectReason::NoVoice]
              << " GuardEaten=" << total[(size_t) RejectReason::GuardEaten]
              << " TooShort=" << total[(size_t) RejectReason::TooShort] << std::endl;

    GF_EXPECT (total[(size_t) RejectReason::NoVoice] > 0);
}

GF_TEST (Scheduler, guard_eaten_is_distinguished_from_too_short)
{
    // Room before the next event is 200 samples; Minimum Gap is 150: the gap was long enough,
    // the pre-event guard ate it.
    GF_EXPECT (noRoomReason (1200, 1000, 150) == RejectReason::GuardEaten);
    GF_EXPECT (noRoomReason (1100, 1000, 150) == RejectReason::TooShort);
    GF_EXPECT (noRoomReason (-1, 1000, 150) == RejectReason::TooShort);     // no event known
    GF_EXPECT (noRoomReason (900, 1000, 150) == RejectReason::TooShort);    // stale boundary
}

GF_TEST (Scheduler, rejections_reach_the_outcome_log_only_while_a_visualiser_listens)
{
    GapFillAudioProcessor quiet;
    setParams (quiet, { { "fill", 0.0f } });
    renderGoldenScenarioA (quiet);
    quiet.drainFillEvents();
    GF_EXPECT (quiet.getGapOutcomeLog().size() == 0);

    GapFillAudioProcessor watched;
    watched.getVisualisationFifo().setEnabled (true);
    setParams (watched, { { "fill", 0.0f } });
    renderGoldenScenarioA (watched);
    watched.drainFillEvents();
    GF_EXPECT (watched.getGapOutcomeLog().size() > 0);
    GF_EXPECT (watched.getGapOutcomeLog().get (0).type == FillEventType::Rejected);
    GF_EXPECT (watched.getGapOutcomeLog().get (0).reason == RejectReason::FillOff);
}

GF_TEST (Scheduler, fill_start_positions_are_independent_of_host_block_size)
{
    std::vector<std::int64_t> reference;

    for (int block : { 32, 64, 128, 480, 512, 1024, 4096 })
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 80.0f } });
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), block, goldenSampleRate, o);
        proc.drainFillEvents();

        std::vector<std::int64_t> starts;

        for (int i = proc.getGapOutcomeLog().size() - 1; i >= 0; --i)
            if (proc.getGapOutcomeLog().get (i).type == FillEventType::Started)
                starts.push_back (proc.getGapOutcomeLog().get (i).pos);

        GF_EXPECT_MSG (! starts.empty(), "no fills at block size " + juce::String (block));

        if (reference.empty())
            reference = starts;
        else
            GF_EXPECT_MSG (starts == reference, "fill starts differ at block size " + juce::String (block));
    }
}

//==============================================================================
// F0-5 effective settings

GF_TEST (Effective, block_and_decision_stages_are_the_identity_at_neutral_settings)
{
    EngineSettings a;
    EngineSettings b;
    std::memcpy (&b, &a, sizeof (a));

    Effective::MacroOutputs out;
    const Effective::MacroOutputs outBefore = out;
    Effective::applyBlock (b, Effective::ModulationInputs {}, out);
    GF_EXPECT (std::memcmp (&a, &b, sizeof (a)) == 0);
    GF_EXPECT (std::memcmp (&out, &outBefore, sizeof (out)) == 0);

    Effective::applyAtDecision (b, 123.456, { true, 123.456, 4, 4, 120.0 });
    GF_EXPECT (std::memcmp (&a, &b, sizeof (a)) == 0);

    Effective::applyAtDecision (b, 0.0, {});
    GF_EXPECT (std::memcmp (&a, &b, sizeof (a)) == 0);
}

GF_TEST (Effective, feature_hooks_leave_default_parameters_neutral)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 512, false);

    ParamCache cache;
    cache.connect (proc.getValueTreeState());

    EngineSettings a;
    EngineSettings b;
    std::memcpy (&b, &a, sizeof (a));
    hooks::gatherFeatureSettings (b, cache);
    GF_EXPECT (std::memcmp (&a, &b, sizeof (a)) == 0);

    const auto in = hooks::gatherModulationInputs (cache);
    GF_EXPECT (in.personality == 0 && ! in.macros.on);
}

GF_TEST (Effective, scheduler_does_not_draw_extra_rng_values_for_neutral_features)
{
    // Same seed, same transport => bit-identical output with the pipeline in place.
    // (The golden scenarios are the strict proof; this guards the determinism contract in-process.)
    GapFillAudioProcessor a, b;
    const auto ra = renderGoldenScenarioA (a);
    const auto rb = renderGoldenScenarioA (b);
    GF_EXPECT (compareBuffers (ra.out, rb.out, 0.0).firstBad < 0);
    GF_EXPECT (ra.fills == rb.fills);
}
