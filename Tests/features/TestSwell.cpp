// N-FX1 Reverse Swell (docs/dev/12 section 4).

#include "../support/TestRig.h"
#include "../support/VoiceRig.h"

#include "features/swell/SwellMode.h"

#include <algorithm>
#include <cmath>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    /** A Reverse voice set up the way fx::prepareVoiceSetup does it. */
    VoiceSetup swellSetup (const VoiceRig& rig, double gapMs, int target, int curve, float filter,
                           fx::SwellTransport tr = {}, fx::SwellLanding* landingOut = nullptr,
                           double sinceMs = 120.0)
    {
        auto s = rig.setup (Algo::Reverse, gapMs, 77);
        s.sinceEventSamples = (int) (kSr * sinceMs * 0.001);
        const auto landing = fx::computeSwellLanding (target, s.lengthSamples, kSr, tr);
        s.modes.swell = true;
        s.modes.swellLanding = landing.landing;
        s.modes.swellFade = landing.fade;
        s.lengthSamples = landing.voiceLength;
        s.fxStart.settings.swell.curve = curve;
        s.fxStart.settings.swell.filter01 = filter;
        s.fxStart.settings.swell.target = target;
        s.fxStart.lengthSamples = s.lengthSamples;

        if (landingOut != nullptr)
            *landingOut = landing;

        return s;
    }

    fx::SwellTransport transportAt (double bpm, double ppqNow)
    {
        fx::SwellTransport t;
        t.valid = true;
        t.ppqNow = ppqNow;
        t.samplesPerBeat = kSr * 60.0 / bpm;
        return t;
    }
}

//==============================================================================
GF_TEST (Swell, swell_off_identity)
{
    auto render = [] (std::initializer_list<std::pair<const char*, float>> p)
    {
        GapFillAudioProcessor proc;
        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = 150.0;
        RenderOptions o;
        o.playHead = &ph;
        return renderOffline (proc, drumLoop (kSr, 150.0, 3), 512, kSr, o).out;
    };

    const auto a = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 1.0f } });
    const auto b = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 1.0f },
                             { "swellAmount", 0.0f }, { "swellCurve", 3.0f }, { "swellFilter", 100.0f }, { "swellTarget", 2.0f } });
    GF_EXPECT (buffersIdentical (a, b));
}

GF_TEST (Swell, flag_is_decided_by_a_salted_sub_rng_and_never_touches_other_recipe_fields)
{
    EngineSettings s;
    GapContext ctx;
    int swelled50 = 0, swelled0 = 0, swelled100 = 0, wrongAlgo = 0;
    const int trials = 4000;

    for (int i = 0; i < trials; ++i)
    {
        ctx.key = hashCombine (0x1234, (std::uint64_t) i);

        FillPlan plan;
        plan.algoIndex = (int) Algo::Reverse;
        plan.recipe.algo = (std::uint8_t) Algo::Reverse;
        plan.recipe.seed = (std::uint32_t) i;
        plan.recipe.lengthMs = 200;
        const FillRecipe before = plan.recipe;

        s.fx.swell.amount01 = 0.0f;
        fx::decideRenderModes (plan, ctx, s);
        swelled0 += (plan.recipe.flags & FillRecipe::flagSwell) != 0;

        s.fx.swell.amount01 = 0.5f;
        fx::decideRenderModes (plan, ctx, s);
        swelled50 += (plan.recipe.flags & FillRecipe::flagSwell) != 0;

        plan.recipe.flags = 0;
        s.fx.swell.amount01 = 1.0f;
        fx::decideRenderModes (plan, ctx, s);
        swelled100 += (plan.recipe.flags & FillRecipe::flagSwell) != 0;

        // Nothing but the flag may change.
        auto after = plan.recipe;
        after.flags &= (std::uint8_t) ~FillRecipe::flagSwell;
        GF_EXPECT (std::memcmp (&after, &before, sizeof (FillRecipe)) == 0);

        // Only Reverse voices ever swell.
        FillPlan other = plan;
        other.recipe.flags = 0;
        other.algoIndex = (int) Algo::Stutter;
        fx::decideRenderModes (other, ctx, s);
        wrongAlgo += other.recipe.flags != 0;
    }

    GF_EXPECT (swelled0 == 0);
    GF_EXPECT (swelled100 == trials);
    GF_EXPECT (wrongAlgo == 0);
    GF_EXPECT_MSG (std::abs (swelled50 - trials / 2) < trials / 20, "probability off: " + juce::String (swelled50));
}

GF_TEST (Swell, locked_recipe_replays_swell)
{
    EngineSettings s;                      // swellAmount 0 and then 100
    GapContext ctx;
    ctx.key = 99;
    FillPlan plan;
    plan.algoIndex = (int) Algo::Reverse;
    plan.recipe.flags = FillRecipe::flagFromLock | FillRecipe::flagSwell;

    fx::decideRenderModes (plan, ctx, s);
    GF_EXPECT ((plan.recipe.flags & FillRecipe::flagSwell) != 0);          // Amount 0 does not strip a recorded swell

    s.transportValid = true;
    s.samplesPerBeat = kSr * 0.5;
    s.ppqAtBlockStart = 1.0;
    ctx.outputPos = 0;
    ctx.blockStartPos = 0;

    VoiceSetup setup;
    setup.algo = Algo::Reverse;
    setup.sampleRate = kSr;
    setup.lengthSamples = 20000;
    fx::prepareVoiceSetup (setup, plan, ctx, s, nullptr);
    GF_EXPECT (setup.modes.swell);

    // ...and a recipe locked without the flag stays plain even at Amount 100.
    plan.recipe.flags = FillRecipe::flagFromLock;
    s.fx.swell.amount01 = 1.0f;
    fx::decideRenderModes (plan, ctx, s);
    GF_EXPECT ((plan.recipe.flags & FillRecipe::flagSwell) == 0);
}

GF_TEST (Swell, peak_lands_at_target_within_1_sample)
{
    for (double bpm : { 60.0, 120.0, 174.0 })
    {
        const auto spb = kSr * 60.0 / bpm;

        for (int target : { 0, 1, 2 })
        {
            for (double ppqNow : { 0.0, 0.37, 1.9, 3.21, 7.0 })
            {
                const auto gap = (int) (kSr * 0.9);
                const auto l = fx::computeSwellLanding (target, gap, kSr, transportAt (bpm, ppqNow));
                const auto gapEnd = gap - l.fade;

                GF_EXPECT (l.landing >= 1 && l.landing <= gapEnd);
                GF_EXPECT (l.voiceLength == l.landing + l.fade && l.voiceLength <= gap);

                if (target == 0 || l.fellBackToGapEnd)
                {
                    GF_EXPECT (l.landing == gapEnd);
                    continue;
                }

                const auto unit = target == 1 ? 1.0 : 4.0;
                const auto landingPpq = ppqNow + (double) l.landing / spb;
                const auto nearest = std::round (landingPpq / unit) * unit;
                GF_EXPECT_MSG (std::abs (landingPpq - nearest) * spb <= 1.0,
                               "bpm " + juce::String (bpm) + " target " + juce::String (target) + " ppq " + juce::String (ppqNow));
                GF_EXPECT (l.landing >= (int) (kSr * 0.06) - 1);           // >= 60 ms lead
                GF_EXPECT (l.landing < gapEnd || std::abs (landingPpq - nearest) * spb <= 1.0);
            }
        }
    }
}

GF_TEST (Swell, voice_envelope_peaks_exactly_at_the_landing_sample)
{
    VoiceRig rig;
    rig.fillDc (0.5f);

    for (int curve = 0; curve < 4; ++curve)
    {
        FillVoice voice;
        voice.prepare (kSr, 2);
        fx::SwellLanding landing;
        const auto s = swellSetup (rig, 400.0, 0, curve, 0.0f, {}, &landing);
        bool started = false;
        const auto wet = rig.render (voice, s, s.lengthSamples + 2000, 64, &started);
        GF_EXPECT (started);
        GF_EXPECT (voice.getSwellMode().isActive());

        int peak = 0;
        float peakValue = 0.0f;

        for (int i = 0; i < wet.getNumSamples(); ++i)
        {
            if (std::abs (wet.getSample (0, i)) > peakValue)
            {
                peakValue = std::abs (wet.getSample (0, i));
                peak = i;
            }
        }

        GF_EXPECT_MSG (std::abs (peak - landing.landing) <= 1,
                       "curve " + juce::String (curve) + " peak " + juce::String (peak) + " target " + juce::String (landing.landing));
        GF_EXPECT (peakValue > 0.3f);

        // Nothing after the landing plus its cosine fade; the voice is over.
        GF_EXPECT (! voice.isActive());
        GF_EXPECT (voice.getLengthSamples() == landing.landing + landing.fade);
        GF_EXPECT (lastAbove (wet, 0.0f) < landing.landing + landing.fade);
    }
}

GF_TEST (Swell, never_overlaps_the_protected_window_for_any_gap)
{
    juce::Random rnd (321);

    for (int i = 0; i < 400; ++i)
    {
        const auto gap = 600 + rnd.nextInt (60000);
        const auto bpm = 50.0 + rnd.nextDouble() * 150.0;
        const auto target = rnd.nextInt (3);
        const auto tr = transportAt (bpm, rnd.nextDouble() * 32.0);

        const auto l = fx::computeSwellLanding (target, gap, kSr, tr);
        GF_EXPECT_MSG (l.voiceLength <= gap, "voice " + juce::String (l.voiceLength) + " > gap " + juce::String (gap));
        GF_EXPECT (l.landing >= 1);
        GF_EXPECT (l.fade >= 4);
    }
}

GF_TEST (Swell, envelope_is_monotonic_until_the_peak_for_every_curve)
{
    for (int curve = 0; curve < 4; ++curve)
    {
        float prev = -1.0f;

        for (int i = 0; i <= 1000; ++i)
        {
            const auto g = fx::swellCurveValue (curve, (float) i / 1000.0f);
            GF_EXPECT (g >= prev);
            GF_EXPECT (g >= 0.0f && g <= 1.0f);
            prev = g;
        }

        GF_EXPECT_NEAR (fx::swellCurveValue (curve, 1.0f), 1.0f, 1.0e-6);
        GF_EXPECT_NEAR (fx::swellCurveValue (curve, 0.0f), 0.0f, 1.0e-6);
    }

    GF_EXPECT (fx::swellCurveValue (1, 0.5f) < fx::swellCurveValue (0, 0.5f));      // exponential is later than linear
    GF_EXPECT (fx::swellCurveValue (3, 0.5f) < fx::swellCurveValue (1, 0.5f));      // late is later still

    // The rendered level never falls before the peak (DC source, filter bypassed).
    VoiceRig rig;
    rig.fillDc (0.5f);
    FillVoice voice;
    voice.prepare (kSr, 2);
    fx::SwellLanding landing;
    const auto s = swellSetup (rig, 400.0, 0, 1, 0.0f, {}, &landing);
    const auto wet = rig.render (voice, s, s.lengthSamples, 32);

    for (int i = (int) (kSr * 0.01); i < landing.landing; ++i)
        GF_EXPECT (wet.getSample (0, i + 1) >= wet.getSample (0, i) - 1.0e-6f);
}

GF_TEST (Swell, high_frequency_energy_rises_with_the_filter)
{
    VoiceRig rig;     // white noise source

    auto hfFraction = [] (const juce::AudioBuffer<float>& b, int from, int count)
    {
        double hf = 0.0, all = 1.0e-12;

        for (int i = from + 1; i < from + count; ++i)
        {
            const double x = b.getSample (0, i), d = x - b.getSample (0, i - 1);
            hf += d * d;
            all += x * x;
        }

        return hf / all;
    };

    auto contrast = [&] (float filter)
    {
        FillVoice voice;
        voice.prepare (kSr, 2);
        fx::SwellLanding landing;
        const auto s = swellSetup (rig, 150.0, 0, 0, filter, {}, &landing, 300.0);
        const auto wet = rig.render (voice, s, s.lengthSamples, 64);
        const auto third = landing.landing / 3;
        const auto early = hfFraction (wet, landing.landing / 6, third / 2);
        const auto late = hfFraction (wet, landing.landing - third / 2, third / 2);
        return late - early;
    };

    GF_EXPECT_MSG (contrast (1.0f) > 0.4, "swept low-pass should open: " + juce::String (contrast (1.0f)));
    GF_EXPECT_MSG (std::abs (contrast (0.0f)) < 0.2, "filter 0 must leave the colour alone: " + juce::String (contrast (0.0f)));
}

GF_TEST (Swell, no_transport_falls_back_to_the_gap_end_and_says_so)
{
    const auto gap = 30000;
    const auto none = fx::computeSwellLanding (2, gap, kSr, {});
    const auto end = fx::computeSwellLanding (0, gap, kSr, {});
    GF_EXPECT (none.noTransport && none.fellBackToGapEnd);
    GF_EXPECT (none.landing == end.landing);
    GF_EXPECT (! end.noTransport);

    // Reported through the meters the status line reads.
    fx::FxMeters meters;
    EngineSettings s;
    s.transportValid = false;
    s.fx.swell.target = 2;
    GapContext ctx;
    FillPlan plan;
    plan.algoIndex = (int) Algo::Reverse;
    plan.recipe.flags = FillRecipe::flagSwell;
    VoiceSetup setup;
    setup.algo = Algo::Reverse;
    setup.sampleRate = kSr;
    setup.lengthSamples = gap;
    fx::prepareVoiceSetup (setup, plan, ctx, s, &meters);
    GF_EXPECT (meters.swellNoTransport.load());
    GF_EXPECT (setup.modes.swellLanding == end.landing);

    // With a transport the landing label is "bar.beat".
    s.transportValid = true;
    s.samplesPerBeat = kSr * 0.5;
    s.ppqAtBlockStart = 64.0 + 0.25;
    s.perf.tsNum = 4;
    s.perf.tsDen = 4;
    fx::prepareVoiceSetup (setup, plan, ctx, s, &meters);
    GF_EXPECT (! meters.swellNoTransport.load());
    GF_EXPECT (meters.swellLandingBar.load() >= 17);
    GF_EXPECT (meters.swellLandingBeat.load() >= 1 && meters.swellLandingBeat.load() <= 4);
}

GF_TEST (Swell, rate_is_clamped_to_x2_and_short_material_is_delayed)
{
    const auto longMaterial = fx::planSwell (40000, 4000, 96);
    GF_EXPECT_NEAR (longMaterial.rate, 2.0, 1.0e-9);
    GF_EXPECT (longMaterial.delay == 0);
    GF_EXPECT_NEAR (longMaterial.startPos, 96.0 + 2.0 * 4000.0, 1.0e-6);       // the newest part is dropped

    const auto shortMaterial = fx::planSwell (2000, 8000, 96);
    GF_EXPECT_NEAR (shortMaterial.rate, 1.0, 1.0e-9);
    GF_EXPECT (shortMaterial.delay > 5000);                                    // leading silence: the peak still lands at T

    // Reading reaches the end index exactly at the landing sample.
    for (const auto& p : { longMaterial, shortMaterial })
    {
        const auto reachAt = (double) p.delay + (p.startPos - (double) p.endIndex) / p.rate;
        GF_EXPECT (std::abs (reachAt - (double) p.landing) <= 1.0);
    }
}

GF_TEST (Swell, block_size_invariant)
{
    VoiceRig rig;
    const auto s = swellSetup (rig, 350.0, 0, 2, 0.8f);
    juce::AudioBuffer<float> first;

    for (int block : { 1, 7, 32, 64, 480, 4096 })
    {
        FillVoice voice;
        voice.prepare (kSr, 2);
        const auto wet = rig.render (voice, s, s.lengthSamples + 100, block);

        if (first.getNumSamples() == 0)
            first.makeCopyOf (wet);
        else
            GF_EXPECT_MSG (buffersIdentical (wet, first), "block size " + juce::String (block));
    }
}

GF_TEST (Swell, panic_fades_without_a_click_and_every_setting_is_finite)
{
    VoiceRig rig;
    FillVoice voice;
    voice.prepare (kSr, 2);
    const auto s = swellSetup (rig, 500.0, 0, 1, 1.0f);
    const auto stopAt = s.lengthSamples / 2;
    const auto fade = (int) (kSr * 0.004);
    const auto wet = rig.render (voice, s, s.lengthSamples + 500, 32, nullptr, stopAt, fade);
    GF_EXPECT (! voice.isActive());
    GF_EXPECT (lastAbove (wet, 1.0e-5f) <= stopAt + fade + 8);
    GF_EXPECT (maxSampleJump (wet) < 1.5f);

    for (int target = 0; target < 3; ++target)
    {
        for (int curve = 0; curve < 4; ++curve)
        {
            for (float filter : { 0.0f, 0.5f, 1.0f })
            {
                for (double ms : { 30.0, 120.0, 800.0, 2500.0 })
                {
                    FillVoice v;
                    v.prepare (kSr, 2);
                    const auto setup = swellSetup (rig, ms, target, curve, filter, transportAt (140.0, 0.31));
                    const auto out = rig.render (v, setup, setup.lengthSamples + 64, 64);
                    GF_EXPECT (allFinite (out));
                    GF_EXPECT (maxAbs (out) < 4.0f);
                }
            }
        }
    }
}
