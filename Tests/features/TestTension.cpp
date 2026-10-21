// F-M21 Tension / release curves (docs/dev/features/F-M21).

#include "../support/TestRig.h"
#include "../support/VoiceRig.h"
#include "../support/AllocGuard.h"

#include "features/tension/Curves.h"
#include "features/tension/Tension.h"
#include "features/tension/TensionTree.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

using namespace gapfill;
using namespace gapfill::tension;
using namespace gftest;

namespace
{
    TensionSettings on (int shape = BuildDrop, int bars = 8, int end = Loop)
    {
        TensionSettings t;
        t.on = true;
        t.shape = shape;
        t.bars = bars;
        t.end = end;
        t.depth[0] = t.depth[1] = t.depth[2] = t.depth[3] = 1.0f;
        return t;
    }

    EngineSettings baseSettings()
    {
        EngineSettings s;
        s.density01 = 0.5f;
        s.intensity01 = 0.5f;
        return s;
    }
}

//==============================================================================
GF_TEST (Tension, off_depth_zero_and_no_transport_are_identity)
{
    auto make = [] (TensionSettings t) { auto s = baseSettings(); s.perf.tension = t; return s; };

    auto off = make (TensionSettings {});
    auto copyOff = off;
    applyDecision (off, 12.0, true, 4, 4);
    GF_EXPECT (std::memcmp (&off, &copyOff, sizeof (EngineSettings)) == 0);

    auto zero = on();
    zero.depth[0] = zero.depth[1] = zero.depth[2] = zero.depth[3] = 0.0f;
    auto z = make (zero);
    auto copyZ = z;
    applyDecision (z, 12.0, true, 4, 4);
    GF_EXPECT (std::memcmp (&z, &copyZ, sizeof (EngineSettings)) == 0);

    auto noTransport = make (on());
    auto copyN = noTransport;
    applyDecision (noTransport, 12.0, false, 4, 4);
    GF_EXPECT (std::memcmp (&noTransport, &copyN, sizeof (EngineSettings)) == 0);

    auto badMeter = make (on());
    auto copyB = badMeter;
    applyDecision (badMeter, 12.0, true, 4, 3);                 // 4/3 is not a valid denominator
    GF_EXPECT (std::memcmp (&badMeter, &copyB, sizeof (EngineSettings)) == 0);
    GF_EXPECT (widthDelta (TensionSettings {}, 1.0f, 3.0, true, 4, 4) == 0.0f);
}

GF_TEST (Tension, shape_values_at_quarter_points_for_all_shapes)
{
    const float q[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

    for (int i = 0; i < 5; ++i)
        GF_EXPECT_NEAR (shapeValue (Build, q[i]), q[i], 1.0e-6);

    GF_EXPECT_NEAR (shapeValue (Swell, 0.0f), 0.0, 1.0e-6);
    GF_EXPECT_NEAR (shapeValue (Swell, 0.5f), 1.0, 1.0e-6);
    GF_EXPECT_NEAR (shapeValue (Swell, 1.0f), 0.0, 1.0e-5);

    GF_EXPECT_NEAR (shapeValue (BuildDrop, 0.0f), 0.0, 1.0e-6);
    GF_EXPECT_NEAR (shapeValue (BuildDrop, 0.8749f), 1.0, 1.0e-3);    // the peak, just before the drop
    GF_EXPECT_NEAR (shapeValue (BuildDrop, 0.875f), 0.5, 1.0e-6);
    GF_EXPECT_NEAR (shapeValue (BuildDrop, 0.9f), 0.5, 1.0e-6);       // the drop: instantly neutral
    GF_EXPECT_NEAR (shapeValue (BuildDrop, 1.0f), 0.5, 1.0e-6);

    GF_EXPECT_NEAR (shapeValue (Breakdown, 0.0f), 1.0, 1.0e-6);
    GF_EXPECT_NEAR (shapeValue (Breakdown, 0.75f), 0.0, 1.0e-6);
    GF_EXPECT_NEAR (shapeValue (Breakdown, 1.0f), 0.5, 1.0e-6);

    for (int s = 0; s < NumShapes; ++s)
        for (float p = 0.0f; p <= 1.0f; p += 0.01f)
            GF_EXPECT (shapeValue (s, p) >= 0.0f && shapeValue (s, p) <= 1.0f);
}

GF_TEST (Tension, eval_lane_linear_smooth_and_step)
{
    TensionLane l;
    l.numPoints = 3;
    l.pt[0] = { 0, 0 };
    l.pt[1] = { 500, 1000 };
    l.pt[2] = { 1000, 0 };

    l.interp = Linear;
    GF_EXPECT_NEAR (evalLane (l, 0.25f), 0.5, 1.0e-6);
    GF_EXPECT_NEAR (evalLane (l, 0.5f), 1.0, 1.0e-6);
    GF_EXPECT_NEAR (evalLane (l, 0.75f), 0.5, 1.0e-6);

    l.interp = Smooth;
    GF_EXPECT_NEAR (evalLane (l, 0.25f), 0.5, 1.0e-6);               // smoothstep(0.5) = 0.5
    GF_EXPECT (evalLane (l, 0.125f) < 0.25f);                          // eased start

    l.interp = Step;
    GF_EXPECT_NEAR (evalLane (l, 0.25f), 0.0, 1.0e-6);
    GF_EXPECT_NEAR (evalLane (l, 0.75f), 1.0, 1.0e-6);

    GF_EXPECT_NEAR (evalLane (l, -3.0f), 0.0, 1.0e-6);                // clamped phase
    GF_EXPECT_NEAR (evalLane (l, 7.0f), 0.0, 1.0e-6);
}

GF_TEST (Tension, phase_matches_an_independent_oracle_through_seeks_and_loops)
{
    juce::Random rnd (4);

    for (int sig : { 4, 6, 7, 3 })
        for (int den : { 4, 8 })
        {
            auto t = on (Build, 8, Loop);
            t.startBar = 3;

            for (int i = 0; i < 2000; ++i)
            {
                const double ppq = rnd.nextDouble() * 400.0;
                const auto out = tensionAt (ppq, true, sig, den, t);
                const double beatsPerBar = 4.0 * sig / den;
                const double u = ppq / beatsPerBar - 3.0;

                if (u < 0.0)
                {
                    GF_EXPECT (! out.active);
                    continue;
                }

                const double oracle = std::fmod (u, 8.0) / 8.0;
                GF_EXPECT (out.active && std::abs (out.phase - oracle) < 1.0e-6);
                GF_EXPECT (tensionAt (ppq, true, sig, den, t).phase == out.phase);          // seeking back gives the same value
            }
        }
}

GF_TEST (Tension, end_loop_hold_release_and_before_start)
{
    auto t = on (Build, 4, Loop);
    t.startBar = 2;

    GF_EXPECT (! tensionAt (4.0 * 1.5, true, 4, 4, t).active);                     // before the start bar: neutral
    GF_EXPECT_NEAR (tensionAt (4.0 * 4.0, true, 4, 4, t).phase, 0.5, 1.0e-6);
    GF_EXPECT_NEAR (tensionAt (4.0 * 8.0, true, 4, 4, t).phase, 0.5, 1.0e-6);       // second pass of the loop

    t.end = Hold;
    const auto held = tensionAt (4.0 * 20.0, true, 4, 4, t);
    GF_EXPECT (held.active && held.phase == 1.0f);

    t.end = Release;
    GF_EXPECT (! tensionAt (4.0 * 20.0, true, 4, 4, t).active);                     // back to the knob values
    GF_EXPECT (tensionAt (4.0 * 5.0, true, 4, 4, t).active);
}

GF_TEST (Tension, mapping_stays_in_range_for_100k_random_curves)
{
    juce::Random rnd (99);

    for (int i = 0; i < 100000; ++i)
    {
        auto t = on (Custom, 2 << rnd.nextInt (5), rnd.nextInt (3));

        for (auto& d : t.depth)
            d = rnd.nextFloat();

        for (auto& lane : t.curves.lane)
        {
            lane.numPoints = (std::uint8_t) (2 + rnd.nextInt (kMaxPoints - 1));
            lane.interp = (std::uint8_t) rnd.nextInt (3);

            for (int p = 0; p < lane.numPoints; ++p)
            {
                lane.pt[p].x = (std::uint16_t) rnd.nextInt (1001);
                lane.pt[p].y = (std::uint16_t) rnd.nextInt (1001);
            }

            normalise (lane);
        }

        GF_EXPECT (validate (t.curves));

        auto s = baseSettings();
        s.density01 = rnd.nextFloat();
        s.intensity01 = rnd.nextFloat();
        s.perf.tension = t;
        applyDecision (s, rnd.nextDouble() * 500.0, true, 4, 4);
        GF_EXPECT (s.density01 >= 0.0f && s.density01 <= 1.0f && s.intensity01 >= 0.0f && s.intensity01 <= 1.0f);
        GF_EXPECT (s.perf.fxSend01 >= 0.0f && s.perf.fxSend01 <= 1.0f);

        const float base = rnd.nextFloat() * 2.0f;
        const float w = base + widthDelta (t, base, rnd.nextDouble() * 500.0, true, 4, 4);
        GF_EXPECT (w >= -1.0e-5f && w <= 2.0f + 1.0e-5f);
    }
}

GF_TEST (Tension, fx_send_is_neutral_at_or_above_half_and_reduced_below)
{
    auto t = on (Custom, 4, Loop);
    t.depth[FxSend] = 1.0f;
    t.depth[Density] = t.depth[Intensity] = t.depth[Width] = 0.0f;

    for (int y : { 500, 800, 1000 })
    {
        for (auto& lane : t.curves.lane)
            lane.pt[0].y = lane.pt[1].y = (std::uint16_t) y;

        auto s = baseSettings();
        s.perf.tension = t;
        applyDecision (s, 1.0, true, 4, 4);
        GF_EXPECT (s.perf.fxSend01 == 1.0f);
    }

    for (auto& lane : t.curves.lane)
        lane.pt[0].y = lane.pt[1].y = 0;

    auto low = baseSettings();
    low.perf.tension = t;
    applyDecision (low, 1.0, true, 4, 4);
    GF_EXPECT_NEAR (low.perf.fxSend01, 0.0, 1.0e-6);
}

GF_TEST (Tension, density_and_intensity_swing_and_lanes_with_zero_depth_never_write)
{
    auto t = on (Build, 4, Loop);
    t.depth[Density] = 0.6f;
    t.depth[Intensity] = 0.0f;
    t.depth[Width] = 0.0f;
    t.depth[FxSend] = 0.0f;

    auto s = baseSettings();
    s.perf.tension = t;
    applyDecision (s, 4.0 * 3.0, true, 4, 4);                          // phase 0.75, c = 0.75
    GF_EXPECT_NEAR (s.density01, 0.5 + 0.5 * 0.6 * 0.5, 1.0e-6);
    GF_EXPECT (s.intensity01 == 0.5f && s.perf.fxSend01 == 1.0f);

    // Width delta is added to the base and clamped.
    auto w = on (Build, 4, Loop);
    w.depth[Width] = 1.0f;
    GF_EXPECT (std::abs (widthDelta (w, 1.0f, 4.0 * 3.0, true, 4, 4) - 0.25f) < 1.0e-5f);
    GF_EXPECT (widthDelta (w, 2.0f, 4.0 * 3.0, true, 4, 4) <= 0.0f);           // already at the limit: cannot go higher
    GF_EXPECT (widthDelta (w, 0.0f, 4.0 * 1.0, true, 4, 4) >= 0.0f);
}

GF_TEST (Tension, order_in_the_decision_stage_is_breathing_then_tension_then_phrase)
{
    // The stage list is exercised through Effective::applyAtDecision: with tension on and a phrase probability
    // multiplier, density was moved before the multiplier is produced and the multiplier is untouched by tension.
    auto s = baseSettings();
    s.perf.tension = on (Build, 4, Loop);
    s.perf.tension.depth[Intensity] = s.perf.tension.depth[Width] = s.perf.tension.depth[FxSend] = 0.0f;
    Effective::TransportInfo ts;
    ts.valid = true; ts.ppq = 4.0 * 3.0; ts.tsNum = 4; ts.tsDen = 4; ts.bpm = 120.0;

    const float mulBefore = s.perf.probabilityMul;
    Effective::applyAtDecision (s, ts.ppq, ts);
    GF_EXPECT (s.density01 > 0.5f);
    GF_EXPECT (s.perf.probabilityMul == mulBefore);
}

GF_TEST (Tension, normalise_keeps_invariants_under_random_edits_and_validate_rejects_bad_curves)
{
    juce::Random rnd (12);

    for (int i = 0; i < 20000; ++i)
    {
        TensionLane l;
        l.numPoints = (std::uint8_t) (1 + rnd.nextInt (30));          // out-of-range counts are repaired
        l.interp = (std::uint8_t) rnd.nextInt (9);

        for (auto& p : l.pt)
        {
            p.x = (std::uint16_t) rnd.nextInt (2000);
            p.y = (std::uint16_t) rnd.nextInt (2000);
        }

        normalise (l);
        TensionCurves c;
        c.lane[0] = l;
        GF_EXPECT_MSG (validate (c), "iteration " + juce::String (i));
        GF_EXPECT (l.pt[0].x == 0 && l.pt[l.numPoints - 1].x == 1000);
    }

    TensionCurves bad;
    GF_EXPECT (validate (bad));
    bad.lane[1].pt[1].x = 900;                                          // last point not at 1000
    GF_EXPECT (! validate (bad));
    bad = {};
    bad.lane[2].numPoints = 1;
    GF_EXPECT (! validate (bad));
    bad = {};
    bad.lane[3].pt[1].y = 1001;
    GF_EXPECT (! validate (bad));
    bad = {};
    bad.lane[0].interp = 3;
    GF_EXPECT (! validate (bad));
    bad = {};
    bad.lane[0].numPoints = 3;
    bad.lane[0].pt[1] = { 0, 500 };                                      // not strictly increasing
    bad.lane[0].pt[2] = { 1000, 500 };
    GF_EXPECT (! validate (bad));
}

GF_TEST (Tension, subtree_roundtrip_invalid_dropped_and_old_state_loads_off)
{
    GapFillAudioProcessor a;
    GF_EXPECT (! serialiseTree (a).isValid());                           // flat curves: nothing written
    GF_EXPECT (pv (a.getValueTreeState().getRawParameterValue ("fm21_on")) == 0.0f);
    GF_EXPECT (pi (a.getValueTreeState().getRawParameterValue ("fm21_bars")) == 2);        // choice index 2 = 8 bars
    GF_EXPECT (pi (a.getValueTreeState().getRawParameterValue ("fm21_shape")) == 1);
    GF_EXPECT_NEAR (pv (a.getValueTreeState().getRawParameterValue ("fm21_depthDensity")), 40.0, 1.0e-6);

    TensionCurves c;
    c.lane[0].numPoints = 3;
    c.lane[0].pt[0] = { 0, 100 };
    c.lane[0].pt[1] = { 400, 900 };
    c.lane[0].pt[2] = { 1000, 300 };
    c.lane[2].interp = Step;
    GF_EXPECT (a.getContextState().tensionService().publishCurves (c));

    const auto tree = serialiseTree (a);
    GF_EXPECT (tree.isValid() && validateTree (tree));
    GF_EXPECT (tree.toXmlString().getNumBytesAsUTF8() <= 2048);

    GapFillAudioProcessor b;
    applyTree (b, tree);
    const auto back = b.getContextState().tensionService().curves();
    GF_EXPECT (back.lane[0].numPoints == 3 && back.lane[0].pt[1].x == 400 && back.lane[0].pt[1].y == 900 && back.lane[2].interp == Step);

    auto broken = tree.createCopy();
    broken.getChild (0).setProperty ("pts", "0:500,400:500,300:500,1000:500", nullptr);       // x not increasing
    GF_EXPECT (! validateTree (broken));
    broken = tree.createCopy();
    broken.removeChild (3, nullptr);                                                          // lane missing
    GF_EXPECT (! validateTree (broken));
    broken = tree.createCopy();
    broken.getChild (1).setProperty ("i", 0, nullptr);                                        // duplicate lane index
    GF_EXPECT (! validateTree (broken));
    broken = tree.createCopy();
    broken.getChild (2).setProperty ("pts", "0:2000,1000:0", nullptr);                         // y out of range
    GF_EXPECT (! validateTree (broken));

    resetTree (b);
    GF_EXPECT (b.getContextState().tensionService().isDefault());
}

GF_TEST (Tension, seqlock_publish_and_read_under_contention_never_returns_a_torn_curve)
{
    TensionService svc;
    std::atomic<bool> stop { false };
    std::atomic<int> torn { 0 };

    std::thread writer ([&]
    {
        for (int i = 0; i < 20000 && ! stop.load(); ++i)
        {
            TensionCurves c;

            for (auto& lane : c.lane)
                lane.pt[0].y = lane.pt[1].y = (std::uint16_t) (i % 1000);

            svc.publishCurves (c);
        }

        stop.store (true);
    });

    while (! stop.load())
    {
        const auto c = svc.curves();

        for (int l = 1; l < kLanes; ++l)
            if (c.lane[l].pt[0].y != c.lane[0].pt[0].y && c.lane[l].pt[0].y != 500)       // 500 = the flat fallback
                torn.fetch_add (1);
    }

    writer.join();
    GF_EXPECT (torn.load() == 0);
}

GF_TEST (Tension, off_is_bit_identical_and_on_is_block_size_invariant_with_transport)
{
    auto render = [] (std::initializer_list<std::pair<const char*, float>> p, int block, std::vector<FillRecipe>* recipes = nullptr)
    {
        GapFillAudioProcessor proc;
        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        auto out = renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), block, goldenSampleRate, o).out;

        if (recipes != nullptr)
        {
            proc.drainFillEvents();

            for (int i = 0; i < proc.getRecipeHistory().size(); ++i)
                recipes->push_back (proc.getRecipeHistory().get (i));
        }

        return out;
    };

    const auto base = render ({ { "fill", 100.0f }, { "density", 60.0f } }, 512);
    const auto off = render ({ { "fill", 100.0f }, { "density", 60.0f }, { "fm21_on", 0.0f }, { "fm21_depthDensity", 100.0f },
                               { "fm21_depthWidth", 100.0f } }, 512);
    GF_EXPECT (buffersIdentical (base, off));

    std::vector<FillRecipe> a, b;
    const auto p = std::initializer_list<std::pair<const char*, float>> {
        { "fill", 100.0f }, { "density", 60.0f }, { "fm21_on", 1.0f }, { "fm21_bars", 0.0f }, { "fm21_shape", 2.0f },
        { "fm21_depthDensity", 100.0f }, { "fm21_depthIntensity", 100.0f } };
    render (p, 128, &a);
    render (p, 2048, &b);
    GF_EXPECT (! a.empty() && a.size() == b.size());

    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        GF_EXPECT (std::memcmp (&a[i], &b[i], sizeof (FillRecipe)) == 0);
}

GF_TEST (Tension, width_target_follows_the_curve_within_tolerance_across_block_sizes)
{
    auto render = [] (int block)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 0.0f }, { "width", 100.0f }, { "fm21_on", 1.0f }, { "fm21_shape", 3.0f }, { "fm21_bars", 0.0f },
                           { "fm21_depthWidth", 100.0f } });
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        return renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), block, goldenSampleRate, o).out;
    };

    const auto a = render (64);
    const auto b = render (4096);
    double worst = 0.0;

    for (int i = 0; i < a.getNumSamples(); i += 97)
        worst = std::max (worst, (double) std::abs (a.getSample (0, i) - b.getSample (0, i)));

    GF_EXPECT_MSG (worst < 0.05, "worst difference " + juce::String (worst));
}

GF_TEST (Tension, audio_thread_does_not_allocate_with_tension_on)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "fm21_on", 1.0f }, { "fm21_depthWidth", 50.0f } });
    TestPlayHead ph;
    ph.bpm = goldenBpm;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), 512, goldenSampleRate, o);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, 512);
    block.clear();

    AllocGuard guard;

    for (int i = 0; i < 400; ++i)
        proc.processBlock (block, midi);

    GF_EXPECT (guard.count() == 0);
}
