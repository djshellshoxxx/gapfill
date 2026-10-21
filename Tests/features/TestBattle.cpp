// F-M18 Beat Battles (docs/dev/12 F-M18).

#include "../support/LiveRig.h"

#include "dsp/FillEngine.h"
#include "features/battle/Battle.h"

#include <cmath>
#include <cstring>

using namespace gapfill;
using namespace gapfill::battle;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    /** An APVTS-style state tree with the given real values. */
    juce::ValueTree stateWith (std::initializer_list<std::pair<const char*, float>> values)
    {
        juce::ValueTree t ("GAPFILL");

        for (const auto& [id, v] : values)
        {
            juce::ValueTree p ("PARAM");
            p.setProperty ("id", id, nullptr);
            p.setProperty ("value", v, nullptr);
            t.appendChild (p, nullptr);
        }

        return t;
    }

    void storeStyles (GapFillAudioProcessor& proc, float fillA, float fillB)
    {
        auto& b = proc.getLiveControl().battles();
        b.setStyleFromState (0, proc.getValueTreeState(), stateWith ({ { "fill", fillA }, { "density", 100.0f } }));
        b.setStyleFromState (1, proc.getValueTreeState(), stateWith ({ { "fill", fillB }, { "density", 100.0f } }));
    }

    struct Render { LiveRender r; Status lastStatus = Status::Off; };

    Render run (int block, int every, bool withStyles, bool playHead, int tsNum, float fillA = 0.0f, float fillB = 100.0f)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 0.0f }, { "dry", 0.0f }, { "battleEvery", (float) every } });

        if (withStyles)
            storeStyles (proc, fillA, fillB);

        TestPlayHead ph;
        ph.provideInfo = playHead;
        ph.tsNum = tsNum;
        ph.tsDen = tsNum > 0 ? 4 : 0;
        const auto input = drumLoop (kSr, 120.0, 6);
        Render out;
        out.r = renderLive (proc, input, block, kSr, ph, {});
        out.lastStatus = proc.getLiveControl().getBattleStatus();
        return out;
    }

    int firstWet (const juce::AudioBuffer<float>& b)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs (b.getSample (0, i)) > 1.0e-7f)
                return i;

        return -1;
    }
}

GF_TEST (Battle, window_parity_follows_the_bar_index_for_every_length)
{
    // 4/4: one bar = 4 quarter notes
    for (int bar = 0; bar < 32; ++bar)
    {
        const auto ppq = bar * 4.0 + 1.5;
        GF_EXPECT (styleForPosition (ppq, 4, 4, 1) == bar % 2);
        GF_EXPECT (styleForPosition (ppq, 4, 4, 2) == (bar / 2) % 2);
        GF_EXPECT (styleForPosition (ppq, 4, 4, 4) == (bar / 4) % 2);
        GF_EXPECT (styleForPosition (ppq, 4, 4, 8) == (bar / 8) % 2);
    }

    GF_EXPECT (styleForPosition (0.0, 4, 4, 1) == 0 && styleForPosition (3.9999, 4, 4, 1) == 0 && styleForPosition (4.0, 4, 4, 1) == 1);
    GF_EXPECT (styleForPosition (3.0, 3, 4, 1) == 1 && styleForPosition (2.99, 3, 4, 1) == 0);        // 3/4: bar = 3 quarters
    GF_EXPECT (styleForPosition (3.0, 6, 8, 1) == 1);                                                  // 6/8: bar = 3 quarters
    GF_EXPECT (styleForPosition (10.0, 4, 4, 0) == -1);

    // pre-roll (negative PPQ) is consistent: bar -1 is odd
    GF_EXPECT (styleForPosition (-0.5, 4, 4, 1) == 1 && styleForPosition (-4.5, 4, 4, 1) == 0);
    GF_EXPECT (styleForPosition (-0.5, 4, 4, 2) == 1);                                                  // window -1
}

GF_TEST (Battle, same_position_gives_the_same_style_after_any_seek_or_loop)
{
    for (double ppq = 0.0; ppq < 200.0; ppq += 0.77)
        for (int every : { 1, 2, 4, 8 })
            GF_EXPECT (styleForPosition (ppq, 4, 4, every) == styleForPosition (ppq + 8.0 * (double) every * 4.0, 4, 4, every));    // period = 2 windows
}

GF_TEST (Battle, decision_stage_replaces_base_values_with_the_active_style_and_is_identity_when_off)
{
    scenes::initTables();
    scenes::initOverlayTargets();
    GapFillAudioProcessor proc;
    BattleModel model;
    model.setStyleFromState (0, proc.getValueTreeState(), stateWith ({ { "fill", 10.0f }, { "chaos", 20.0f } }));
    model.setStyleFromState (1, proc.getValueTreeState(), stateWith ({ { "fill", 90.0f }, { "chaos", 80.0f } }));

    BattleConfig cfg;
    GF_EXPECT (model.publisher().consume (cfg) && cfg.differ == 1);

    BattleRuntime rt;
    rt.adopt (cfg);
    rt.everyBars = 1;

    Effective::TransportInfo ts;
    ts.valid = true;
    ts.tsNum = 4;
    ts.tsDen = 4;

    EngineSettings s;
    s.perf.battle = &rt;
    ts.ppq = 1.0;
    Effective::applyAtDecision (s, 1.0, ts);
    GF_EXPECT_NEAR (s.fill01, 0.10, 1e-5);
    GF_EXPECT_NEAR (s.chaos01, 0.20, 1e-5);

    EngineSettings s2;
    s2.perf.battle = &rt;
    Effective::applyAtDecision (s2, 5.0, ts);
    GF_EXPECT_NEAR (s2.fill01, 0.90, 1e-5);
    GF_EXPECT_NEAR (s2.chaos01, 0.80, 1e-5);

    // no time signature / no transport: untouched
    EngineSettings before, s3;
    s3.perf.battle = &rt;
    auto noTs = ts;
    noTs.tsNum = 0;
    Effective::applyAtDecision (s3, 5.0, noTs);
    GF_EXPECT (s3.fill01 == before.fill01);
    noTs = ts;
    noTs.valid = false;
    Effective::applyAtDecision (s3, 5.0, noTs);
    GF_EXPECT (s3.fill01 == before.fill01);

    // no battle runtime attached: bit-identical settings
    EngineSettings off, ref;
    Effective::applyAtDecision (off, 5.0, ts);
    GF_EXPECT (std::memcmp (&off, &ref, sizeof (EngineSettings)) == 0);
}

GF_TEST (Battle, off_is_identical_to_a_plugin_without_styles)
{
    const auto offWithStyles = run (512, 0, true, true, 4);
    const auto offWithout = run (512, 0, false, true, 4);
    GF_EXPECT (compareBuffers (offWithStyles.r.out, offWithout.r.out, 0.0).maxAbs == 0.0);
    GF_EXPECT (offWithStyles.lastStatus == Status::Off);
}

GF_TEST (Battle, alternates_styles_per_bar_and_never_writes_the_parameters)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 0.0f }, { "dry", 0.0f }, { "battleEvery", 1.0f } });     // choice index 1 = every bar
    storeStyles (proc, 0.0f, 100.0f);
    const auto before = proc.getValueTreeState().copyState().createCopy();

    TestPlayHead ph;
    ph.tsNum = 4;
    ph.tsDen = 4;
    const auto input = drumLoop (kSr, 120.0, 6);
    const auto r = renderLive (proc, input, 512, kSr, ph, {});

    // 120 bpm: one bar = 2 s. A (fill 0) silences even bars; B plays in odd bars.
    const auto first = firstWet (r.out);
    GF_EXPECT_MSG (first >= (int) (2.0 * kSr), "wet starts in the first B bar (sample " + juce::String (first) + ")");

    double energyBarA = 0.0, energyBarB = 0.0;

    for (int i = 0; i < (int) (2.0 * kSr); ++i)
        energyBarA += std::abs (r.out.getSample (0, i));

    for (int i = (int) (2.0 * kSr); i < (int) (4.0 * kSr); ++i)
        energyBarB += std::abs (r.out.getSample (0, i));

    GF_EXPECT (energyBarA == 0.0 && energyBarB > 0.0);

    // overlay only: nothing in the APVTS moved
    for (const auto& id : allParameterIDs())
    {
        const auto node = before.getChildWithProperty ("id", id);
        const float now = proc.getValueTreeState().getRawParameterValue (id)->load();
        GF_EXPECT_MSG (! node.isValid() || std::abs ((float) node.getProperty ("value") - now) < 1.0e-5f, id);
    }
}

GF_TEST (Battle, results_are_block_size_invariant)
{
    const auto ref = run (512, 1, true, true, 4);
    GF_EXPECT (ref.r.fills > 0);

    for (const int block : { 32, 64, 4096 })
    {
        const auto other = run (block, 1, true, true, 4);
        GF_EXPECT_MSG (other.r.fills == ref.r.fills, "block " + juce::String (block));
        GF_EXPECT_MSG (firstWet (other.r.out) == firstWet (ref.r.out), "block " + juce::String (block));
    }
}

GF_TEST (Battle, inactive_without_transport_or_time_signature_and_says_why)
{
    const auto noTransport = run (512, 1, true, false, 4);
    GF_EXPECT (noTransport.lastStatus == Status::NoTransport);
    GF_EXPECT (noTransport.r.fills == 0);                      // base Fill is 0 and the styles did not apply

    const auto noTs = run (512, 1, true, true, 0);
    GF_EXPECT (noTs.lastStatus == Status::NoTransport && noTs.r.fills == 0);

    const auto active = run (512, 1, true, true, 4);
    GF_EXPECT (active.lastStatus == Status::StyleA || active.lastStatus == Status::StyleB);
    GF_EXPECT (juce::String (statusText (active.lastStatus)).startsWith ("Battle: "));
}

GF_TEST (Battle, inactive_until_two_different_styles_are_stored)
{
    const auto none = run (512, 1, false, true, 4);
    GF_EXPECT (none.lastStatus == Status::NeedsStyles && none.r.fills == 0);

    const auto same = run (512, 1, true, true, 4, 50.0f, 50.0f);          // A == B
    GF_EXPECT (same.lastStatus == Status::NeedsStyles && same.r.fills == 0);

    GapFillAudioProcessor proc;
    auto& b = proc.getLiveControl().battles();
    b.setStyleFromCurrent (0, proc.getValueTreeState());
    GF_EXPECT (b.hasStyle (0) && ! b.hasStyle (1) && ! b.stylesDiffer());
    setParams (proc, { { "chaos", 90.0f } });
    b.setStyleFromCurrent (1, proc.getValueTreeState());
    GF_EXPECT (b.stylesDiffer());
    b.clear();
    GF_EXPECT (! b.hasStyle (0) && ! b.stylesDiffer());
}

GF_TEST (Battle, styles_only_hold_scene_scope_parameters)
{
    scenes::initTables();
    GapFillAudioProcessor proc;
    BattleModel m;
    m.setStyleFromState (0, proc.getValueTreeState(), stateWith ({ { "dry", 7.0f }, { "wet", 9.0f }, { "fill", 55.0f } }));
    m.setStyleFromState (1, proc.getValueTreeState(), stateWith ({ { "fill", 56.0f } }));
    BattleConfig c;
    GF_EXPECT (m.publisher().consume (c));

    for (int i = 0; i < c.nA; ++i)
    {
        const auto& id = scenes::paramIdAt (c.idxA[i]);
        GF_EXPECT_MSG (! scenes::isNeverId (id), id);
    }
}

GF_TEST (Battle, panic_suspends_the_battle)
{
    GapFillAudioProcessor proc;
    storeStyles (proc, 0.0f, 100.0f);
    auto& live = proc.getLiveControl();
    prepareProcessor (proc, kSr, 256, false);

    FillEngine engine;
    engine.prepare (kSr, 256, 2);
    juce::MidiBuffer midi;

    EngineSettings s;
    s.transportValid = true;
    s.samplesPerBeat = 24000.0;
    s.perf.tsNum = 4;
    s.perf.tsDen = 4;
    s.perf.battleEvery = 1;
    live.prepareChunk (s, 256, 0);
    GF_EXPECT (s.perf.battle != nullptr);

    live.panic().requestPanic();
    live.beginBlock (midi, engine, 0);
    EngineSettings s2 = s;
    s2.perf.battle = nullptr;
    live.prepareChunk (s2, 256, 0);
    GF_EXPECT (s2.perf.battle == nullptr && s2.perf.panicked);

    live.panic().requestRecover();
    live.beginBlock (midi, engine, 0);
    EngineSettings s3;
    s3.perf.battleEvery = 1;
    s3.transportValid = true;
    s3.samplesPerBeat = 24000.0;
    s3.perf.tsNum = 4;
    s3.perf.tsDen = 4;
    live.prepareChunk (s3, 256, 0);
    GF_EXPECT (s3.perf.battle != nullptr && ! s3.perf.panicked);
}

GF_TEST (Battle, parameter_is_registered_off_by_default_and_a_performance_id)
{
    GapFillAudioProcessor proc;
    auto* p = proc.getValueTreeState().getParameter ("battleEvery");
    GF_EXPECT (p != nullptr && p->getDefaultValue() == 0.0f);
    GF_EXPECT (performanceParameterIDs().contains ("battleEvery") && ! randomisableParameterIDs().contains ("battleEvery"));
    GF_EXPECT (p->getNumSteps() == 5);
}
