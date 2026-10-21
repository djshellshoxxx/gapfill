// C-4 Fill Intensity Envelope (docs/dev/04-SPEC-CREATIVE.md C-4).

#include "../support/TestRig.h"

#include "features/envelope/Envelope.h"

#include <cmath>
#include <cstring>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr float noKnots[envelope::numKnots] { 0, 0, 0, 0, 0 };

    float maxStep (const std::vector<float>& v)
    {
        float m = 0.0f;
        for (size_t i = 1; i < v.size(); ++i)
            m = std::max (m, std::abs (v[i] - v[i - 1]));
        return m;
    }

    std::vector<float> runStage (int shape, float amount, std::uint64_t seed, int n, double sr, float freq)
    {
        envelope::EnvelopeStage stage;
        stage.start (shape, amount, sr, seed);
        std::vector<float> out ((size_t) n);

        for (int i = 0; i < n; ++i)
        {
            float l = 0.5f * std::sin (juce::MathConstants<float>::twoPi * freq * (float) i / (float) sr), r = l;
            stage.process (l, r, (float) i / (float) n);
            out[(size_t) i] = l;
        }

        return out;
    }
}

GF_TEST (Envelope, shape_values_at_0_50_100)
{
    using namespace envelope;
    const float k[numKnots] { 0.2f, 0.9f, 0.4f, 0.7f, 0.1f };

    for (float t : { 0.0f, 0.5f, 1.0f })
        GF_EXPECT_NEAR (shapeAt (Flat, t, noKnots), 1.0, 1.0e-6);

    GF_EXPECT_NEAR (shapeAt (Rise, 0.0f, noKnots), 0.0, 1.0e-6);
    GF_EXPECT_NEAR (shapeAt (Rise, 0.5f, noKnots), 0.5, 1.0e-6);
    GF_EXPECT_NEAR (shapeAt (Rise, 1.0f, noKnots), 1.0, 1.0e-6);
    GF_EXPECT_NEAR (shapeAt (Fall, 0.0f, noKnots), 1.0, 1.0e-6);
    GF_EXPECT_NEAR (shapeAt (Fall, 0.5f, noKnots), 0.5, 1.0e-6);
    GF_EXPECT_NEAR (shapeAt (Fall, 1.0f, noKnots), 0.0, 1.0e-6);
    GF_EXPECT_NEAR (shapeAt (RiseFall, 0.0f, noKnots), 0.0, 1.0e-6);
    GF_EXPECT_NEAR (shapeAt (RiseFall, 0.5f, noKnots), 1.0, 1.0e-6);
    GF_EXPECT_NEAR (shapeAt (RiseFall, 1.0f, noKnots), 0.0, 1.0e-5);
    GF_EXPECT_NEAR (shapeAt (RandomWalk, 0.0f, k), 0.2, 1.0e-6);     // knot 0
    GF_EXPECT_NEAR (shapeAt (RandomWalk, 0.5f, k), 0.4, 1.0e-6);     // knot 2
    GF_EXPECT_NEAR (shapeAt (RandomWalk, 1.0f, k), 0.1, 1.0e-6);     // knot 4
    GF_EXPECT_NEAR (shapeAt (RandomWalk, 0.25f, k), 0.9, 1.0e-6);    // knot 1

    // Out-of-range ages clamp, NaN is safe.
    GF_EXPECT_NEAR (shapeAt (Rise, -3.0f, noKnots), 0.0, 1.0e-9);
    GF_EXPECT_NEAR (shapeAt (Rise, 9.0f, noKnots), 1.0, 1.0e-9);
    GF_EXPECT (std::isfinite (shapeAt (RiseFall, std::nanf (""), noKnots)));
}

GF_TEST (Envelope, random_walk_repeatable_by_seed_and_varies_by_seed)
{
    float a[envelope::numKnots], b[envelope::numKnots], c[envelope::numKnots];
    envelope::makeKnots (1234, a);
    envelope::makeKnots (1234, b);
    envelope::makeKnots (1235, c);
    GF_EXPECT (std::memcmp (a, b, sizeof (a)) == 0);
    GF_EXPECT (std::memcmp (a, c, sizeof (a)) != 0);

    for (float k : a)
        GF_EXPECT (k >= 0.0f && k < 1.0f);

    // Through the stage: same voice seed -> identical output, different seed -> different.
    const auto x = runStage (envelope::RandomWalk, 1.0f, 77, 4800, 48000.0, 440.0f);
    const auto y = runStage (envelope::RandomWalk, 1.0f, 77, 4800, 48000.0, 440.0f);
    const auto z = runStage (envelope::RandomWalk, 1.0f, 78, 4800, 48000.0, 440.0f);
    GF_EXPECT (x == y);
    GF_EXPECT (x != z);
}

GF_TEST (Envelope, flat_or_zero_amount_is_inactive_and_bit_identical)
{
    envelope::EnvelopeStage flat, zero;
    flat.start (envelope::Flat, 1.0f, 48000.0, 1);
    zero.start (envelope::Rise, 0.0f, 48000.0, 1);
    GF_EXPECT (! flat.isActive() && ! zero.isActive());

    float l = 0.3f, r = -0.7f;
    flat.process (l, r, 0.4f);
    zero.process (l, r, 0.4f);
    GF_EXPECT (l == 0.3f && r == -0.7f);

    // Whole plugin: explicit Flat / 0 % with a 100 % amount behind Flat is identical to the defaults.
    GapFillAudioProcessor a, b, c;
    setParams (a, { { "fill", 85.0f } });
    setParams (b, { { "fill", 85.0f }, { "fillShape", 0.0f }, { "fillShapeAmount", 100.0f } });
    setParams (c, { { "fill", 85.0f }, { "fillShape", 3.0f }, { "fillShapeAmount", 0.0f } });
    const auto ra = renderGoldenScenarioA (a);
    const auto rb = renderGoldenScenarioA (b);
    const auto rc = renderGoldenScenarioA (c);
    GF_EXPECT (ra.fills > 0);
    GF_EXPECT (compareBuffers (ra.out, rb.out, 0.0).maxAbs == 0.0);
    GF_EXPECT (compareBuffers (ra.out, rc.out, 0.0).maxAbs == 0.0);
}

GF_TEST (Envelope, level_and_cutoff_stay_within_their_bounds)
{
    for (float a : { 0.0f, 0.25f, 0.5f, 1.0f })
    {
        for (float s = 0.0f; s <= 1.0f; s += 0.05f)
        {
            const auto lv = envelope::levelFor (s, a);
            const auto fc = envelope::cutoffFor (s, a);
            GF_EXPECT (lv >= 1.0f - a - 1.0e-6f && lv <= 1.0f + 1.0e-6f);
            GF_EXPECT (fc >= 20000.0f * std::pow (0.1f, a) - 0.01f && fc <= 20000.0f + 0.01f);
        }
    }

    GF_EXPECT_NEAR (envelope::levelFor (0.0f, 1.0f), 0.0, 1.0e-9);
    GF_EXPECT_NEAR (envelope::cutoffFor (0.0f, 1.0f), 2000.0, 0.01);
    GF_EXPECT_NEAR (envelope::cutoffFor (1.0f, 1.0f), 20000.0, 0.01);
}

GF_TEST (Envelope, stage_output_is_quieter_and_darker_at_low_shape_values)
{
    // Rise at 100 %: the start is silent-ish and dark, the end is close to the input.
    const auto out = runStage (envelope::Rise, 1.0f, 5, 9600, 48000.0, 5000.0f);
    float early = 0.0f, late = 0.0f;

    for (int i = 0; i < 960; ++i)       early = std::max (early, std::abs (out[(size_t) i]));
    for (int i = 8640; i < 9600; ++i)   late  = std::max (late,  std::abs (out[(size_t) i]));

    GF_EXPECT (early < 0.25f * late);
    GF_EXPECT (late > 0.2f);
}

GF_TEST (Envelope, no_click_for_every_shape)
{
    for (int shape = 1; shape < envelope::NumShapes; ++shape)
    {
        for (double sr : { 44100.0, 48000.0, 192000.0 })
        {
            const auto in = runStage (envelope::Flat, 0.0f, 1, 6000, sr, 200.0f);   // plain sine
            const auto out = runStage (shape, 1.0f, 99, 6000, sr, 200.0f);
            GF_EXPECT_MSG (maxStep (out) <= maxStep (in) * 1.05f + 1.0e-4f,
                           "shape " + juce::String (shape) + " sr " + juce::String (sr));

            for (float v : out)
                GF_EXPECT (std::isfinite (v));
        }
    }
}

GF_TEST (Envelope, stage_survives_nan_amount_and_extreme_shape)
{
    envelope::EnvelopeStage s;
    s.start (99, std::nanf (""), 48000.0, 1);
    GF_EXPECT (! s.isActive());
    s.start (-4, 5.0f, 48000.0, 1);
    GF_EXPECT (! s.isActive());      // negative shape clamps to Flat
    s.start (envelope::Fall, 5.0f, 48000.0, 1);   // amount clamps to 1
    float l = 0.5f, r = 0.5f;
    s.process (l, r, 0.7f);
    GF_EXPECT (std::isfinite (l) && std::isfinite (r));
}

GF_TEST (Envelope, works_with_short_and_long_fills_and_leaves_protection_alone)
{
    for (float maxFill : { 120.0f, 1800.0f })
    {
        GapFillAudioProcessor off, on;
        setParams (off, { { "fill", 90.0f }, { "maxFill", maxFill } });
        setParams (on,  { { "fill", 90.0f }, { "maxFill", maxFill }, { "fillShape", 3.0f }, { "fillShapeAmount", 100.0f } });

        std::vector<float> duckOff, duckOn;
        RenderOptions oOff, oOn;
        oOff.onBlock = [&] (int, int) { duckOff.push_back (off.getFillEngine().getCurrentDuck()); };
        oOn.onBlock  = [&] (int, int) { duckOn.push_back (on.getFillEngine().getCurrentDuck()); };

        const auto in = drumLoop (48000.0, 150.0, 4);
        const auto a = renderOffline (off, in, 512, 48000.0, oOff);
        const auto b = renderOffline (on, in, 512, 48000.0, oOn);

        GF_EXPECT (b.fills == a.fills && b.fills > 0);
        GF_EXPECT (duckOff == duckOn);   // protection / duck independent of the envelope
        GF_EXPECT (compareBuffers (a.out, b.out, 0.0).maxAbs > 0.0);

        for (int ch = 0; ch < b.out.getNumChannels(); ++ch)
            for (int i = 0; i < b.out.getNumSamples(); ++i)
                GF_EXPECT (std::isfinite (b.out.getSample (ch, i)));
    }
}
