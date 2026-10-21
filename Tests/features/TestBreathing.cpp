// C-6 Density Breathing (docs/dev/04-SPEC-CREATIVE.md C-6).

#include "../support/TestRig.h"

#include "features/breathing/Breathing.h"

#include <cmath>
#include <cstring>

using namespace gapfill;
using namespace gftest;

namespace
{
    Effective::TransportInfo transport (int num = 4, int den = 4, bool valid = true)
    {
        return { valid, 0.0, num, den, 120.0 };
    }

    EngineSettings withBreath (int bars = 4, float depth = 1.0f, int curve = 0, float density = 0.5f)
    {
        EngineSettings s;
        s.density01 = density;
        s.perf.breath.on = true;
        s.perf.breath.bars = bars;
        s.perf.breath.depth01 = depth;
        s.perf.breath.curve = curve;
        return s;
    }

    std::vector<std::int64_t> startsFor (int block, std::initializer_list<std::pair<const char*, float>> params)
    {
        GapFillAudioProcessor proc;
        setParams (proc, params);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        ph.tsNum = 4; ph.tsDen = 4;
        RenderOptions o;
        o.playHead = &ph;
        renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, 8), block, goldenSampleRate, o);
        proc.drainFillEvents();

        std::vector<std::int64_t> starts;

        for (int i = proc.getGapOutcomeLog().size() - 1; i >= 0; --i)
            if (proc.getGapOutcomeLog().get (i).type == FillEventType::Started)
                starts.push_back (proc.getGapOutcomeLog().get (i).pos);

        return starts;
    }
}

GF_TEST (Breathing, neutral_when_disabled_depth0_or_no_transport)
{
    auto expectUntouched = [] (EngineSettings s, double ppq, Effective::TransportInfo t, const char* what)
    {
        EngineSettings before;
        std::memcpy (&before, &s, sizeof (s));
        breathing::apply (s, ppq, t);
        GF_EXPECT_MSG (std::memcmp (&before, &s, sizeof (s)) == 0, what);
    };

    auto off = withBreath(); off.perf.breath.on = false;
    expectUntouched (off, 1.0, transport(), "disabled");
    expectUntouched (withBreath (4, 0.0f), 1.0, transport(), "depth 0");
    expectUntouched (withBreath(), 1.0, transport (4, 4, false), "invalid transport");
    expectUntouched (withBreath(), 1.0, transport (0, 0), "unknown time signature");
    expectUntouched (withBreath(), 1.0, transport (4, 3), "denominator not a power of two");
    expectUntouched (withBreath(), std::nan (""), transport(), "NaN ppq");

    // And through the whole plugin: on with depth 0 is the default sound.
    GapFillAudioProcessor a, b;
    setParams (a, { { "fill", 80.0f } });
    setParams (b, { { "fill", 80.0f }, { "densityBreath", 1.0f }, { "densityBreathDepth", 0.0f } });
    GF_EXPECT (compareBuffers (renderGoldenScenarioA (a).out, renderGoldenScenarioA (b).out, 0.0).maxAbs == 0.0);
}

GF_TEST (Breathing, curve_values_at_quarter_points)
{
    const double p[4] { 0.0, 0.25, 0.5, 0.75 };
    const float sine[4] { 0.0f, 1.0f, 0.0f, -1.0f };
    const float tri[4] { -1.0f, 0.0f, 1.0f, 0.0f };
    const float pulse[4] { 1.0f, 1.0f, -1.0f, -1.0f };

    for (int i = 0; i < 4; ++i)
    {
        GF_EXPECT_NEAR (breathing::curveValue (breathing::Sine, p[i]), sine[i], 1.0e-6);
        GF_EXPECT_NEAR (breathing::curveValue (breathing::RiseFall, p[i]), tri[i], 1.0e-6);
        GF_EXPECT_NEAR (breathing::curveValue (breathing::Pulse, p[i]), pulse[i], 1.0e-6);
    }

    // Phase wraps and survives garbage.
    GF_EXPECT_NEAR (breathing::curveValue (breathing::Sine, 1.25), 1.0, 1.0e-6);
    GF_EXPECT_NEAR (breathing::curveValue (breathing::Sine, -0.75), 1.0, 1.0e-6);
    GF_EXPECT (std::isfinite (breathing::curveValue (breathing::Sine, std::nan (""))));
}

GF_TEST (Breathing, density_follows_the_formula_and_is_anchored_at_ppq_zero)
{
    // 4 bars of 4/4 = 16 beats: ppq 4 is a quarter of the cycle (sine peak).
    auto s = withBreath (4, 1.0f, breathing::Sine, 0.5f);
    breathing::apply (s, 4.0, transport());
    GF_EXPECT_NEAR (s.density01, 1.0, 1.0e-6);               // 0.5 + 0.5 * 1 * 1 clamps to 1.0

    auto t = withBreath (4, 0.5f, breathing::Sine, 0.5f);
    breathing::apply (t, 4.0, transport());
    GF_EXPECT_NEAR (t.density01, 0.75, 1.0e-6);              // 0.5 + 0.5 * 0.5 * 1

    auto u = withBreath (4, 0.5f, breathing::Sine, 0.5f);
    breathing::apply (u, 12.0, transport());
    GF_EXPECT_NEAR (u.density01, 0.25, 1.0e-6);              // trough
}

GF_TEST (Breathing, same_ppq_same_density)
{
    for (double ppq : { 0.0, 1.7, 9.25, 31.5 })
    {
        auto a = withBreath(), b = withBreath();
        breathing::apply (a, ppq, transport());
        breathing::apply (b, ppq, transport());
        GF_EXPECT (a.density01 == b.density01);
    }
}

GF_TEST (Breathing, loop_wrap_continuity)
{
    for (int bars : { 1, 2, 4, 8 })
    {
        const double cycle = bars * 4.0;      // beats in one breathing cycle of 4/4

        for (double ppq : { 0.3, 2.9, 7.7 })
        {
            auto a = withBreath (bars, 0.8f, breathing::RiseFall), b = a;
            breathing::apply (a, ppq, transport());
            breathing::apply (b, ppq + 3.0 * cycle, transport());
            GF_EXPECT_NEAR (a.density01, b.density01, 1.0e-5);
        }
    }
}

GF_TEST (Breathing, density_never_leaves_zero_to_one)
{
    for (float base : { 0.0f, 0.3f, 1.0f })
        for (float depth : { 0.0f, 0.4f, 1.0f, 5.0f })
            for (int curve = 0; curve < breathing::NumCurves; ++curve)
                for (double ppq = -8.0; ppq < 40.0; ppq += 0.37)
                {
                    auto s = withBreath (2, depth, curve, base);
                    breathing::apply (s, ppq, transport());
                    GF_EXPECT (s.density01 >= 0.0f && s.density01 <= 1.0f);
                }
}

GF_TEST (Breathing, time_signature_6_8_and_7_4)
{
    // 6/8 = 3 quarter beats per bar: 2 bars = 6 beats, sine peak at 1.5 beats.
    auto a = withBreath (2, 1.0f, breathing::Sine, 0.5f);
    a.density01 = 0.2f;
    breathing::apply (a, 1.5, transport (6, 8));
    GF_EXPECT_NEAR (a.density01, 0.7, 1.0e-6);

    // 7/4 = 7 beats per bar: 1 bar = 7 beats, sine trough at 5.25.
    auto b = withBreath (1, 1.0f, breathing::Sine, 0.5f);
    breathing::apply (b, 5.25, transport (7, 4));
    GF_EXPECT_NEAR (b.density01, 0.0, 1.0e-6);

    GF_EXPECT_NEAR (breathing::phaseAt (b.perf.breath, 7.0, transport (7, 4)), 0.0, 1.0e-9);
}

GF_TEST (Breathing, block_size_invariant_decisions)
{
    std::vector<std::int64_t> reference;

    for (int block : { 32, 64, 512, 4096, 8192 })
    {
        const auto starts = startsFor (block, { { "fill", 80.0f }, { "densityBreath", 1.0f }, { "densityBreathDepth", 100.0f },
                                                 { "densityBreathBars", 0.0f } });
        GF_EXPECT_MSG (! starts.empty(), "no fills at block " + juce::String (block));

        if (reference.empty())
            reference = starts;
        else
            GF_EXPECT_MSG (starts == reference, "starts differ at block " + juce::String (block));
    }
}

GF_TEST (Breathing, changes_decisions_when_active_and_publishes_the_phase)
{
    const auto plain = startsFor (512, { { "fill", 80.0f } });
    const auto breathingStarts = startsFor (512, { { "fill", 80.0f }, { "densityBreath", 1.0f },
                                                    { "densityBreathDepth", 100.0f }, { "densityBreathBars", 0.0f } });
    GF_EXPECT (plain != breathingStarts);

    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 80.0f }, { "densityBreath", 1.0f }, { "densityBreathDepth", 100.0f } });
    TestPlayHead ph;
    ph.bpm = goldenBpm;
    ph.tsNum = 4; ph.tsDen = 4;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), goldenBlockSize, goldenSampleRate, o);
    const auto& t = proc.getFillEngine().getDecisionTelemetry();
    GF_EXPECT (t.decisions.load() > 0);
    GF_EXPECT (t.breathPhase.load() >= 0.0f && t.breathPhase.load() < 1.0f);
}
