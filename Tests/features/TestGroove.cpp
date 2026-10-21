// F-L09 Groove inheritance (docs/dev/features/F-L09).

#include "../support/TestRig.h"
#include "../support/VoiceRig.h"
#include "../support/AllocGuard.h"

#include "features/context/ContextHooks.h"
#include "features/groove/GrooveState.h"
#include "features/groove/GrooveStateTree.h"
#include "features/groove/GrooveTracker.h"
#include "features/groove/GrooveWarp.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace gapfill;
using namespace gftest;
using namespace gapfill::groove;

namespace
{
    /** Feeds `bars` bars of events on the grid, odd slots late by `offsetSteps` (+ optional jitter in steps). */
    void feedPattern (GrooveTracker& t, int grid, double offsetSteps, int bars, double jitterSteps = 0.0,
                      std::uint32_t seed = 5, double startPpq = 0.0)
    {
        juce::Random rnd ((juce::int64) seed);
        const double g = grid == 0 ? 0.5 : 0.25;
        const int steps = (int) (bars * 4.0 / g);

        for (int k = 0; k < steps; ++k)
        {
            double x = (k % 2 == 1) ? offsetSteps : 0.0;

            if (jitterSteps > 0.0)
                x += (rnd.nextDouble() * 2.0 - 1.0) * jitterSteps;

            const double ppq = startPpq + ((double) k + x) * g;
            t.tick (ppq, true);
            t.onEvent (ppq, 0.8f);
        }
    }

    /** Swung hats as audio: one burst per 1/16 step, odd steps late. */
    juce::AudioBuffer<float> swungHats (double sr, double bpm, double offsetSteps, int bars)
    {
        const double stepSec = 60.0 / bpm * 0.25;
        const int n = (int) (sr * bars * 4.0 * 60.0 / bpm);
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        juce::Random rnd (11);

        for (int k = 0; k < bars * 16; ++k)
        {
            const double t = ((double) k + ((k % 2 == 1) ? offsetSteps : 0.0)) * stepSec;
            const int start = (int) std::lround (t * sr);
            const int len = (int) (0.012 * sr);

            for (int i = 0; i < len && start + i < n; ++i)
            {
                const float env = (float) std::exp (-(double) i / (0.003 * sr));
                const float v = 0.7f * env * (rnd.nextFloat() * 2.0f - 1.0f);
                b.setSample (0, start + i, v);
                b.setSample (1, start + i, v);
            }
        }

        return b;
    }

    GrooveSnapshot makeSnap (float swingOffset, bool valid = true)
    {
        GrooveSnapshot s;

        for (int i = 0; i < kSlots; ++i)
            s.offsetSteps[i] = (i % 2 == 1) ? swingOffset : 0.0f;

        s.confidence = valid ? 0.9f : 0.1f;
        s.swingPercent = 50.0f + 50.0f * swingOffset;
        s.valid = valid;
        return s;
    }
}

//==============================================================================
GF_TEST (Groove, straight_input_zero_offsets)
{
    for (int grid : { 0, 1 })
    {
        GrooveTracker t;
        t.setGrid (grid);
        feedPattern (t, grid, 0.0, 4);
        const auto s = t.snapshot();

        for (int i = 0; i < kSlots; ++i)
            GF_EXPECT (std::abs (s.offsetSteps[i]) <= 0.02f);

        GF_EXPECT (std::abs (s.swingPercent - 50.0f) <= 1.0f);
    }
}

GF_TEST (Groove, swing_58_62_667_recovered_1_8_and_1_16)
{
    for (int grid : { 0, 1 })
        for (double swing : { 0.58, 0.62, 2.0 / 3.0 })
        {
            const double off = 2.0 * swing - 1.0;               // steps
            GrooveTracker t;
            t.setGrid (grid);
            feedPattern (t, grid, off, 4);
            const auto s = t.snapshot();

            GF_EXPECT (s.valid);

            for (int i = 0; i < kSlots; ++i)
                GF_EXPECT_MSG (std::abs (s.offsetSteps[i] - ((i % 2 == 1) ? off : 0.0)) <= 0.05,
                               "grid " + juce::String (grid) + " swing " + juce::String (swing) + " slot " + juce::String (i));

            GF_EXPECT_MSG (std::abs ((double) s.swingPercent - swing * 100.0) <= 3.0, juce::String (s.swingPercent));
        }
}

GF_TEST (Groove, offgrid_notes_ignored)
{
    GrooveTracker t;
    t.setGrid (1);

    for (int k = 0; k < 64; ++k)
        t.onEvent ((k + 0.45) * 0.25, 1.0f);                  // 0.45 step late: a ghost / triplet note

    GF_EXPECT (t.acceptedEvents() == 0);
    GF_EXPECT (! t.snapshot().valid);

    t.onEvent (10.0 * 0.25 + 0.39 * 0.25, 1.0f);
    GF_EXPECT (t.acceptedEvents() == 1);
}

GF_TEST (Groove, jitter_not_treated_as_swing)
{
    // +-3 ms of random jitter on a straight 1/16 pattern, from 90 to 150 BPM.
    for (double bpm : { 90.0, 120.0, 150.0 })
    {
        const double stepMs = 60000.0 / bpm * 0.25;
        GrooveTracker t;
        t.setGrid (1);
        feedPattern (t, 1, 0.0, 8, 3.0 / stepMs, 17);
        GF_EXPECT_MSG (t.snapshot().swingPercent <= 52.0f, "bpm " + juce::String (bpm) + " swing " + juce::String (t.snapshot().swingPercent));
    }
}

GF_TEST (Groove, transport_jump_clears_and_settles)
{
    GrooveTracker t;
    t.setGrid (1);
    feedPattern (t, 1, 0.24, 4);
    GF_EXPECT (t.snapshot().valid);

    t.onTransportJump();
    GF_EXPECT (! t.snapshot().valid);

    // Events before the settle window is armed by a tick are ignored ...
    t.onEvent (100.0, 1.0f);
    GF_EXPECT (t.acceptedEvents() == 0);

    // ... and so are events within one beat after the first tick.
    t.tick (100.0, true);
    t.onEvent (100.5, 1.0f);
    GF_EXPECT (t.acceptedEvents() == 0);
    t.onEvent (101.25, 1.0f);
    GF_EXPECT (t.acceptedEvents() == 1);
}

GF_TEST (Groove, seek_landing_midbar_no_false_groove)
{
    GrooveTracker t;
    t.setGrid (1);
    feedPattern (t, 1, 0.24, 4);
    t.onTransportJump();
    t.tick (37.13, true);                                    // landed in the middle of a beat
    feedPattern (t, 1, 0.0, 2, 0.0, 3, 40.0);               // straight bars afterwards: nothing from the old groove survives
    GF_EXPECT (std::abs (t.snapshot().swingPercent - 50.0f) <= 3.0f);
}

GF_TEST (Groove, hold_load_restores_offsets_and_idle_decay_halves_n)
{
    GrooveTracker a;
    a.setGrid (1);
    feedPattern (a, 1, 0.24, 4);
    const auto snap = a.snapshot();

    GrooveTracker b;
    b.setGrid (1);
    b.load (snap);

    for (int i = 0; i < kSlots; ++i)
        GF_EXPECT (std::abs (b.snapshot().offsetSteps[i] - snap.offsetSteps[i]) < 1.0e-6f);

    // A learned (not held) tracker forgets slots untouched for 16 bars.
    a.tick (1000.0, true);
    a.tick (2000.0, true);
    a.tick (3000.0, true);
    GF_EXPECT (! a.snapshot().valid);
}

GF_TEST (Groove, warp_monotonic_max_slope_below_1)
{
    juce::Random rnd (4);

    for (int trial = 0; trial < 50; ++trial)
    {
        GrooveSnapshot s;
        s.valid = true;
        s.confidence = 1.0f;

        for (int i = 0; i < kSlots; ++i)
            s.offsetSteps[i] = (rnd.nextFloat() * 2.0f - 1.0f) * kMaxOffsetSteps;

        for (int grid : { 0, 1 })
        {
            const double h = 1.0e-3;
            double prevWarp = warpBeats (s, grid, 0.0, 1.0f);

            for (double p = h; p < 8.0; p += h)
            {
                const double w = warpBeats (s, grid, p, 1.0f);
                GF_EXPECT ((p + w) > (p - h + prevWarp));                        // strictly increasing positions
                const double slope = (w - prevWarp) / h;
                GF_EXPECT (slope <= 0.7 + 1.0e-6 && slope >= -0.7 - 1.0e-6);
                prevWarp = w;
            }
        }
    }

    // Amount scales the shift linearly; invalid or amount 0 shifts nothing.
    const auto s = makeSnap (0.3f);
    GF_EXPECT_NEAR (warpBeats (s, 1, 0.25, 0.5f), 0.5 * warpBeats (s, 1, 0.25, 1.0f), 1.0e-9);
    GF_EXPECT (warpBeats (makeSnap (0.3f, false), 1, 0.25, 1.0f) == 0.0);
    GF_EXPECT (warpBeats (s, 1, 0.25, 0.0f) == 0.0);
}

GF_TEST (Groove, shifts_clamped_and_increasing)
{
    const auto snap = makeSnap (0.4f);
    GrooveShifts out;

    for (double period : { 0.25, 0.5, 1.0 })
        for (double start : { 0.0, 0.13, 3.7, 1000.001 })
        {
            makeShifts (snap, 1, start, period, 24000.0, 1.0f, out);
            GF_EXPECT (out.on && out.count == kMaxShifts);
            GF_EXPECT (out.shift[0] == 0);                       // the fill start never moves

            const double periodSamples = period * 24000.0;

            for (int k = 1; k < kMaxShifts; ++k)
            {
                GF_EXPECT ((double) std::abs (out.shift[k]) <= 0.35 * periodSamples + 1.0);
                const double advance = periodSamples + (double) (out.shift[k] - out.shift[k - 1]);
                GF_EXPECT (advance >= 0.3 * periodSamples - 1.0);
            }
        }

    // Gate: invalid snapshot, amount 0, bad period -> off and nothing written.
    makeShifts (makeSnap (0.3f, false), 1, 0.0, 0.25, 24000.0, 1.0f, out);
    GF_EXPECT (! out.on && out.delta (0) == 0 && out.at (3) == 0);
    makeShifts (snap, 1, 0.0, 0.25, 24000.0, 0.0f, out);
    GF_EXPECT (! out.on);
    makeShifts (snap, 1, 0.0, 0.0, 24000.0, 1.0f, out);
    GF_EXPECT (! out.on);
}

GF_TEST (Groove, sync_off_and_inactive_settings_write_no_shift)
{
    EngineSettings s;
    s.transportValid = true;
    s.samplesPerBeat = 24000.0;
    s.perf.groove.amount01 = 1.0f;
    s.perf.groove.snap = makeSnap (0.3f);
    s.perf.groove.syncOn = false;                                // Sync off: no musical subdivision exists

    GapContext ctx;
    FillPlan plan;
    VoiceSetup setup;
    setup.algo = Algo::Stutter;
    setup.stutterSeconds = 0.125f;
    setup.sampleRate = 48000.0;

    context::prepareVoiceSetup (setup, plan, ctx, s, SourceHistory(), nullptr);
    GF_EXPECT (! setup.grooveShifts.on);

    s.perf.groove.syncOn = true;
    context::prepareVoiceSetup (setup, plan, ctx, s, SourceHistory(), nullptr);
    GF_EXPECT (setup.grooveShifts.on);

    // Algorithms without a subdivision grid ignore it.
    VoiceSetup other;
    other.algo = Algo::Granular;
    other.sampleRate = 48000.0;
    context::prepareVoiceSetup (other, plan, ctx, s, SourceHistory(), nullptr);
    GF_EXPECT (! other.grooveShifts.on);

    // Invalid snapshot (confidence gate): offsets are 0.
    s.perf.groove.snap = makeSnap (0.3f, false);
    VoiceSetup gated;
    gated.algo = Algo::Delay;
    gated.delaySeconds = 0.125f;
    gated.sampleRate = 48000.0;
    context::prepareVoiceSetup (gated, plan, ctx, s, SourceHistory(), nullptr);
    GF_EXPECT (! gated.grooveShifts.on);
}

GF_TEST (Groove, stutter_and_delay_follow_the_warp_and_other_algorithms_ignore_it)
{
    VoiceRig rig;
    auto render = [&] (Algo algo, const GrooveShifts& gs)
    {
        FillVoice v;
        v.prepare (kRigSr, 2);
        auto s = rig.setup (algo, 600.0, 31);
        s.stutterSeconds = 0.125f;
        s.delaySeconds = 0.125f;
        s.grooveShifts = gs;
        return rig.render (v, s, (int) (kRigSr * 0.6), 64);
    };

    GrooveShifts none;
    GrooveShifts shifted;
    makeShifts (makeSnap (0.3f), 1, 0.0, 0.25, 24000.0, 1.0f, shifted);       // 0.25 beat = 125 ms at 120 BPM
    GF_EXPECT (shifted.on);

    for (Algo algo : { Algo::Stutter, Algo::Delay })
    {
        const auto a = render (algo, none);
        const auto b = render (algo, shifted);
        const auto b2 = render (algo, shifted);
        GF_EXPECT (! buffersIdentical (a, b));                    // the subdivisions moved ...
        GF_EXPECT (buffersIdentical (b, b2));                     // ... deterministically
        GF_EXPECT (std::abs (rmsOf (a, 0, 28000) - rmsOf (b, 0, 28000)) < 0.5 * rmsOf (a, 0, 28000) + 1.0e-6);   // same material, only re-timed
    }

    for (Algo algo : { Algo::Granular, Algo::Reverse, Algo::Freeze, Algo::PitchRise, Algo::Filter })
        GF_EXPECT (buffersIdentical (render (algo, none), render (algo, shifted)));

    // Explicitly neutral shifts (on = false, any contents) are the existing path.
    GrooveShifts off = shifted;
    off.on = false;
    GF_EXPECT (buffersIdentical (render (Algo::Stutter, none), render (Algo::Stutter, off)));
}

GF_TEST (Groove, groove_off_is_bit_identical)
{
    auto render = [] (std::initializer_list<std::pair<const char*, float>> p)
    {
        GapFillAudioProcessor proc;
        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        return renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), 512, goldenSampleRate, o).out;
    };

    const auto a = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "syncEnable", 1.0f } });
    const auto b = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "syncEnable", 1.0f },
                             { "fl09_amount", 0.0f }, { "fl09_grid", 0.0f }, { "fl09_hold", 1.0f } });
    GF_EXPECT (buffersIdentical (a, b));
}

GF_TEST (Groove, processor_recovers_swing_with_and_without_lookahead)
{
    const double sr = 48000.0, bpm = 120.0;
    const auto audio = swungHats (sr, bpm, 0.24, 8);

    for (float lookahead : { 0.0f, 40.0f })
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fl09_amount", 100.0f }, { "fl09_grid", 1.0f }, { "syncEnable", 1.0f },
                           { "fill", 0.0f }, { "lookahead", lookahead } });
        TestPlayHead ph;
        ph.bpm = bpm;
        RenderOptions o;
        o.playHead = &ph;
        renderOffline (proc, audio, 512, sr, o);

        GrooveSnapshot snap;
        GF_EXPECT (proc.getContextState().grooveState().readSnapshot (snap));
        GF_EXPECT_MSG (snap.valid, "lookahead " + juce::String (lookahead));
        GF_EXPECT_MSG (std::abs (snap.swingPercent - 62.0f) <= 3.0f, "swing " + juce::String (snap.swingPercent) + " at " + juce::String (lookahead) + " ms");

        for (int i = 0; i < kSlots; ++i)
            GF_EXPECT_MSG (std::abs (snap.offsetSteps[i] - ((i % 2 == 1) ? 0.24f : 0.0f)) <= 0.06f,
                           "slot " + juce::String (i) + " = " + juce::String (snap.offsetSteps[i]));
    }
}

GF_TEST (Groove, estimates_are_block_size_invariant)
{
    const double sr = 48000.0, bpm = 120.0;
    const auto audio = swungHats (sr, bpm, 0.24, 8);
    GrooveSnapshot ref;
    bool haveRef = false;

    for (int block : { 64, 512, 4096 })
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fl09_amount", 100.0f }, { "fl09_grid", 1.0f }, { "fill", 0.0f } });
        TestPlayHead ph;
        ph.bpm = bpm;
        RenderOptions o;
        o.playHead = &ph;
        renderOffline (proc, audio, block, sr, o);

        GrooveSnapshot snap;
        GF_EXPECT (proc.getContextState().grooveState().readSnapshot (snap));

        if (! haveRef)
        {
            ref = snap;
            haveRef = true;
            continue;
        }

        for (int i = 0; i < kSlots; ++i)
            GF_EXPECT_MSG (std::abs (snap.offsetSteps[i] - ref.offsetSteps[i]) < 0.01f, "block " + juce::String (block));
    }
}

GF_TEST (Groove, no_tracking_without_transport)
{
    const double sr = 48000.0;
    GapFillAudioProcessor proc;
    setParams (proc, { { "fl09_amount", 100.0f }, { "fill", 0.0f } });
    TestPlayHead ph;
    ph.bpm = 120.0;
    ph.playing = false;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, swungHats (sr, 120.0, 0.24, 4), 512, sr, o);

    GrooveSnapshot snap;
    proc.getContextState().grooveState().readSnapshot (snap);
    GF_EXPECT (! snap.valid);
    GF_EXPECT (proc.getContextState().grooveState().isWaitingForTempo());
}

GF_TEST (Groove, learn_command_measures_then_holds_and_reset_forgets)
{
    const double sr = 48000.0, bpm = 120.0;
    GapFillAudioProcessor proc;
    setParams (proc, { { "fl09_amount", 100.0f }, { "fl09_grid", 1.0f }, { "fill", 0.0f } });
    TestPlayHead ph;
    ph.bpm = bpm;

    Command learn;
    learn.type = CommandType::GrooveLearn;

    RenderOptions o;
    o.playHead = &ph;
    o.onBlock = [&] (int block, int) { if (block == 0) proc.postCommand (learn); };
    renderOffline (proc, swungHats (sr, bpm, 0.24, 12), 512, sr, o);          // 12 bars > 8 bars of learning

    auto& gs = proc.getContextState().grooveState();
    GF_EXPECT (gs.isFrozenByLearn());
    GF_EXPECT (gs.learnBarsRemaining() == 0);

    GrooveSnapshot snap;
    GF_EXPECT (gs.readSnapshot (snap) && snap.valid);
    GF_EXPECT (std::abs (snap.swingPercent - 62.0f) <= 3.0f);

    // The learned groove is saved in the <Groove> subtree and restores in a fresh instance.
    const auto tree = serialiseTree (proc);
    GF_EXPECT (tree.isValid() && validateTree (tree));

    GapFillAudioProcessor other;
    setParams (other, { { "fl09_amount", 100.0f } });
    applyTree (other, tree);
    TestPlayHead ph2;
    ph2.bpm = bpm;
    RenderOptions o2;
    o2.playHead = &ph2;
    renderOffline (other, juce::AudioBuffer<float> (2, 4096), 512, sr, o2);
    GrooveSnapshot restored;
    GF_EXPECT (other.getContextState().grooveState().readSnapshot (restored) && restored.valid);

    for (int i = 0; i < kSlots; ++i)
        GF_EXPECT (std::abs (restored.offsetSteps[i] - snap.offsetSteps[i]) <= 0.002f);

    // RESET forgets everything.
    Command reset;
    reset.type = CommandType::GrooveReset;
    RenderOptions o3;
    ph.ppq = 0.0;
    o3.playHead = &ph;
    o3.onBlock = [&] (int block, int) { if (block == 0) proc.postCommand (reset); };
    renderOffline (proc, juce::AudioBuffer<float> (2, 4096), 512, sr, o3);
    GF_EXPECT (! proc.getContextState().grooveState().isFrozenByLearn());
}

GF_TEST (Groove, state_subtree_validation_and_old_state)
{
    GapFillAudioProcessor fresh;
    GF_EXPECT (! serialiseTree (fresh).isValid());                 // nothing learned / held: nothing written

    auto make = [] (const char* offsets, const char* conf = "0.7", int version = 1)
    {
        juce::ValueTree t ("Groove");
        t.setProperty ("v", version, nullptr);
        t.setProperty ("hold", 1, nullptr);
        t.setProperty ("conf", conf, nullptr);
        t.setProperty ("o", offsets, nullptr);
        return t;
    };

    GF_EXPECT (validateTree (make ("0,0.2,0,0.2,0,0.2,0,0.2")));
    GF_EXPECT (! validateTree (make ("0,0.2,0,0.2,0,0.2,0")));                   // 7 values
    GF_EXPECT (! validateTree (make ("0,0.9,0,0.2,0,0.2,0,0.2")));               // out of range
    GF_EXPECT (! validateTree (make ("0,nan,0,0.2,0,0.2,0,0.2")));               // not numeric
    GF_EXPECT (! validateTree (make ("0,0.2,0,0.2,0,0.2,0,0.2", "1.5")));        // confidence out of range
    GF_EXPECT (! validateTree (make ("0,0.2,0,0.2,0,0.2,0,0.2", "0.7", 2)));     // unknown version
    GF_EXPECT (! validateTree (juce::ValueTree ("Other")));

    // Hold on without a learned groove writes nothing.
    GapFillAudioProcessor held;
    setParams (held, { { "fl09_hold", 1.0f } });
    GF_EXPECT (! serialiseTree (held).isValid());

    // Old states (no Groove subtree, no groove parameters) leave the feature off.
    GF_EXPECT (pv (fresh.getValueTreeState().getRawParameterValue ("fl09_amount")) == 0.0f);
    GF_EXPECT (pi (fresh.getValueTreeState().getRawParameterValue ("fl09_grid")) == 1);
    const auto perf = performanceParameterIDs();

    for (const char* id : { "fl09_amount", "fl09_grid", "fl09_hold" })
        GF_EXPECT (perf.contains (id) && ! randomisableParameterIDs().contains (id));
}

GF_TEST (Groove, only_wet_subdivisions_move_fill_starts_and_dry_unchanged)
{
    const double sr = goldenSampleRate;
    auto render = [&] (float amount, float wet)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "syncEnable", 1.0f }, { "wet", wet },
                           { "fl09_amount", amount } });

        // A learned groove, loaded the way a saved project would.
        proc.getContextState().grooveState().requestLoad (makeSnap (0.3f), true);

        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        auto r = renderOffline (proc, drumLoop (sr, goldenBpm, goldenBars), 512, sr, o);
        proc.drainFillEvents();
        std::vector<FillRecipe> recipes;

        for (int i = 0; i < proc.getRecipeHistory().size(); ++i)
            recipes.push_back (proc.getRecipeHistory().get (i));

        return std::make_pair (std::move (r), recipes);
    };

    const auto base = render (0.0f, 100.0f);
    const auto swung = render (100.0f, 100.0f);

    GF_EXPECT (base.first.fills == swung.first.fills);                // same fills at the same times
    GF_EXPECT (base.second.size() == swung.second.size());

    for (size_t i = 0; i < base.second.size() && i < swung.second.size(); ++i)
        GF_EXPECT (std::memcmp (&base.second[i], &swung.second[i], sizeof (FillRecipe)) == 0);

    // With the wet signal removed the two renders are bit-identical: the dry path never moves.
    const auto dryBase = render (0.0f, 0.0f);
    const auto drySwung = render (100.0f, 0.0f);
    GF_EXPECT (buffersIdentical (dryBase.first.out, drySwung.first.out));
}

GF_TEST (Groove, audio_thread_does_not_allocate_while_tracking)
{
    const double sr = 48000.0, bpm = 120.0;
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "syncEnable", 1.0f }, { "fl09_amount", 100.0f } });
    TestPlayHead ph;
    ph.bpm = bpm;
    RenderOptions o;
    o.playHead = &ph;
    const auto audio = swungHats (sr, bpm, 0.24, 4);
    renderOffline (proc, audio, 512, sr, o);                  // warm up

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, 512);
    block.clear();

    AllocGuard guard;

    for (int i = 0; i < 400; ++i)
        proc.processBlock (block, midi);

    GF_EXPECT (guard.count() == 0);
}
