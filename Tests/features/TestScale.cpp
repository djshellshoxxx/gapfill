// N-MUS Scale Lock (docs/dev/12 section 5).

#include "../support/TestRig.h"
#include "../support/VoiceRig.h"
#include "../support/AllocGuard.h"

#include "features/scale/ScaleHooks.h"
#include "features/scale/ScaleLock.h"
#include "features/scale/VoiceQuantiser.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace gapfill;
using namespace gftest;

namespace
{
    int mod12 (int v) { return ((v % 12) + 12) % 12; }

    EngineSettings settingsFor (int root, int type, float amount01 = 1.0f)
    {
        EngineSettings s;
        s.scaleLock.set = scale::makeScaleSet (root, type);
        s.scaleLock.amount01 = amount01;
        return s;
    }

    FillPlan pitchPlan (Algo algo, int band = -1)
    {
        FillPlan p;
        p.algoIndex = (int) algo;
        p.band = band;
        p.recipe.algo = (std::uint8_t) algo;
        p.recipe.band = (std::int8_t) band;
        p.recipe.lengthMs = 200;
        return p;
    }

    juce::AudioBuffer<float> sceneAudio() { return drumLoop (goldenSampleRate, goldenBpm, goldenBars); }

    std::vector<FillRecipe> renderRecipes (std::initializer_list<std::pair<const char*, float>> params, int block)
    {
        GapFillAudioProcessor proc;
        setParams (proc, params);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        renderOffline (proc, sceneAudio(), block, goldenSampleRate, o);
        proc.drainFillEvents();

        std::vector<FillRecipe> out;

        for (int i = 0; i < proc.getRecipeHistory().size(); ++i)
            out.push_back (proc.getRecipeHistory().get (i));

        return out;
    }
}

//==============================================================================
GF_TEST (Scale, scale_off_identity)
{
    // Off with every other scale parameter moved: output bit-identical to defaults.
    auto render = [] (std::initializer_list<std::pair<const char*, float>> p)
    {
        GapFillAudioProcessor proc;
        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        return renderOffline (proc, sceneAudio(), 512, goldenSampleRate, o).out;
    };

    const auto a = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 6.0f } });
    const auto b = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 6.0f },
                             { "scaleRoot", 9.0f }, { "scaleType", 0.0f }, { "scaleAmount", 37.0f } });
    GF_EXPECT (buffersIdentical (a, b));

    // Amount 0 is neutral as well.
    const auto c = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 6.0f },
                             { "scaleRoot", 2.0f }, { "scaleType", 2.0f }, { "scaleAmount", 0.0f } });
    GF_EXPECT (buffersIdentical (a, c));
}

GF_TEST (Scale, rng_untouched_when_off)
{
    EngineSettings s;                                   // type Off
    GapContext ctx;
    ctx.key = 1234;

    auto plan = pitchPlan (Algo::PitchRise);
    plan.recipe.seed = 77;
    const auto before = plan.recipe;
    scale::decidePitch (plan, ctx, s);
    GF_EXPECT (std::memcmp (&plan.recipe, &before, sizeof (FillRecipe)) == 0);

    VoiceSetup setup;
    scale::prepareVoiceSetup (setup, plan, s);
    GF_EXPECT (setup.pitchOverride == 127);
    GF_EXPECT (! setup.scaleLock.active());

    // The identity holds for the maths and for the per-voice quantiser as well.
    GF_EXPECT (scale::snapInterval (13, s.scaleLock.set) == 13);
    GF_EXPECT (scale::snapFrequency (123.0f, s.scaleLock.set) == 123.0f);
    scale::VoiceQuantiser q;
    q.begin (s.scaleLock, 5);
    GF_EXPECT (! q.active() && q.snapRatio (0.8409f) == 0.8409f && q.snapCents (37.0f) == 37.0f);

    // A voice with the lock off keeps its derived pitch (render is finite and identical per start).
    VoiceRig rig;
    FillVoice a, b;
    a.prepare (kRigSr, 2);
    b.prepare (kRigSr, 2);
    auto setupA = rig.setup (Algo::Granular, 200.0);
    auto setupB = setupA;
    setupB.scaleLock = settingsFor (0, 0, 0.5f).scaleLock;      // Off with a non-default amount
    GF_EXPECT (buffersIdentical (rig.render (a, setupA, 9600), rig.render (b, setupB, 9600)));
}

GF_TEST (Scale, snapped_interval_in_scale_for_every_scale_and_root)
{
    for (int type = 1; type < scale::kNumScaleTypes; ++type)
        for (int root = 0; root < 12; ++root)
        {
            const auto set = scale::makeScaleSet (root, type);
            GF_EXPECT (set.active());

            for (int i = -48; i <= 48; ++i)
            {
                const auto snapped = scale::snapInterval (i, set);
                GF_EXPECT_MSG (set.containsPitchClass (root + snapped), "scale " + juce::String (type) + " root " + juce::String (root)
                                                                           + " i " + juce::String (i));
                GF_EXPECT (std::abs (snapped - i) <= 6);
                GF_EXPECT (scale::snapInterval (snapped, set) == snapped);          // idempotent
            }
        }
}

GF_TEST (Scale, tie_rule_deterministic)
{
    const auto cMajor = scale::makeScaleSet (0, (int) scale::ScaleType::Major);       // 0 2 4 5 7 9 11

    GF_EXPECT (scale::snapInterval (1, cMajor) == 0);             // 0 and 2 tie: smaller |i|
    GF_EXPECT (scale::snapInterval (-1, cMajor) == -1);           // B is a degree
    GF_EXPECT (scale::snapInterval (6, cMajor) == 5);             // 5 and 7 tie: smaller |i|
    GF_EXPECT (scale::snapInterval (-6, cMajor) == -5);           // -7 and -5 tie: smaller |i|
    GF_EXPECT (scale::snapInterval (3, cMajor) == 2);             // 2 and 4 tie: smaller |i|
    GF_EXPECT (scale::snapInterval (8, cMajor) == 7);             // 7 and 9 tie: smaller |i|
    GF_EXPECT (scale::snapInterval (10, cMajor) == 9);            // 9 and 11 tie: smaller |i|

    // With the source on C# (not a degree) 0 stays allowed and +1 (D) is in key.
    scale::PitchContext offScale; offScale.sourcePc = 1;
    GF_EXPECT (scale::snapInterval (0, cMajor, offScale) == 0);
    GF_EXPECT (scale::snapInterval (1, cMajor, offScale) == 1);

    // Same input twice, same answer.
    for (int i = -30; i <= 30; ++i)
        GF_EXPECT (scale::snapInterval (i, cMajor) == scale::snapInterval (i, cMajor));
}

GF_TEST (Scale, octaves_preserved)
{
    for (int type = 1; type < scale::kNumScaleTypes; ++type)
    {
        const auto set = scale::makeScaleSet (3, type);

        for (int i = -12; i <= 12; ++i)
            for (int oct : { -24, -12, 12, 24 })
                {
                    // Same snapping distance one or more octaves away, and still in key (ties may resolve toward zero).
                    const auto r = scale::snapInterval (i + oct, set);
                    GF_EXPECT (std::abs (r - (i + oct)) == std::abs (scale::snapInterval (i, set) - i));
                    GF_EXPECT (set.containsPitchClass (3 + r));
                }
    }
}

GF_TEST (Scale, zero_and_octave_always_allowed)
{
    for (int type = 1; type < scale::kNumScaleTypes; ++type)
        for (int root = 0; root < 12; ++root)
        {
            const auto set = scale::makeScaleSet (root, type);

            for (int k : { -36, -24, -12, 0, 12, 24, 36 })
                GF_EXPECT (scale::snapInterval (k, set) == k);

            // Also with a source pitch class that is not in the scale.
            for (int pc = 0; pc < 12; ++pc)
            {
                scale::PitchContext ctx; ctx.sourcePc = pc;
                GF_EXPECT (scale::snapInterval (0, set, ctx) == 0);
                GF_EXPECT (scale::snapInterval (12, set, ctx) == 12);
                GF_EXPECT (set.containsPitchClass (pc + scale::snapInterval (7, set, ctx)) || scale::snapInterval (7, set, ctx) % 12 == 0);
            }
        }
}

GF_TEST (Scale, amount_0_never_snaps_100_always)
{
    GapContext ctx;
    int changedAt100 = 0;

    for (int i = 0; i < 2000; ++i)
    {
        ctx.key = hashCombine (0xABCD, (std::uint64_t) i);

        auto s0 = settingsFor (2, (int) scale::ScaleType::NaturalMinor, 0.0f);
        auto p0 = pitchPlan (Algo::PitchRise);
        scale::decidePitch (p0, ctx, s0);
        GF_EXPECT (p0.recipe.pitchSemis == 0 && (p0.recipe.flags & FillRecipe::flagSnapped) == 0);    // lock inactive: voice derives

        auto s1 = settingsFor (2, (int) scale::ScaleType::NaturalMinor, 1.0f);
        auto p1 = pitchPlan (Algo::PitchRise);
        scale::decidePitch (p1, ctx, s1);
        GF_EXPECT (s1.scaleLock.set.containsPitchClass (2 + p1.recipe.pitchSemis));
        changedAt100 += (p1.recipe.flags & FillRecipe::flagSnapped) != 0;
        GF_EXPECT (isValid (p1.recipe));
    }

    GF_EXPECT (changedAt100 > 100);          // a good share of raw draws were out of key
}

GF_TEST (Scale, amount_50_ratio_within_tolerance_over_10k_draws)
{
    GapContext ctx;
    const int trials = 10000;
    int wouldChange = 0, changedAt50 = 0;

    for (int i = 0; i < trials; ++i)
    {
        ctx.key = hashCombine (0x5151, (std::uint64_t) i);
        auto full = settingsFor (7, (int) scale::ScaleType::Blues, 1.0f);
        auto half = full;
        half.scaleLock.amount01 = 0.5f;
        full.chaos01 = half.chaos01 = 0.6f;

        auto a = pitchPlan (Algo::PitchFall);
        auto b = pitchPlan (Algo::PitchFall);
        scale::decidePitch (a, ctx, full);
        scale::decidePitch (b, ctx, half);

        const bool changes = (a.recipe.flags & FillRecipe::flagSnapped) != 0;
        wouldChange += changes;
        changedAt50 += (b.recipe.flags & FillRecipe::flagSnapped) != 0;

        // A decision that was not snapped keeps the raw draw (same sub-RNG draw as the 100% case).
        if (! ((b.recipe.flags & FillRecipe::flagSnapped) != 0))
            GF_EXPECT (b.recipe.pitchSemis == a.recipe.pitchSemis || changes);
    }

    GF_EXPECT (wouldChange > 1000);
    const double ratio = (double) changedAt50 / (double) wouldChange;
    GF_EXPECT_MSG (std::abs (ratio - 0.5) < 0.05, "ratio " + juce::String (ratio));
}

GF_TEST (Scale, recipe_pitch_matches_snapped)
{
    GapContext ctx;
    auto s = settingsFor (2, (int) scale::ScaleType::NaturalMinor);       // D minor

    for (int i = 0; i < 300; ++i)
    {
        ctx.key = hashCombine (0x77, (std::uint64_t) i);

        for (Algo algo : { Algo::PitchRise, Algo::PitchFall })
        {
            auto plan = pitchPlan (algo);
            scale::decidePitch (plan, ctx, s);

            GF_EXPECT ((plan.recipe.pitchSemis > 0) == (algo == Algo::PitchRise));

            VoiceSetup setup;
            scale::prepareVoiceSetup (setup, plan, s);
            GF_EXPECT (setup.pitchOverride == plan.recipe.pitchSemis);
            GF_EXPECT (setup.scaleLock.active());

            // Round trip through the recipe codec (Lock / History / Audition replay).
            std::uint8_t bytes[FillRecipe::serialisedBytes];
            toBytes (plan.recipe, bytes);
            const auto back = fromBytes (bytes);
            GF_EXPECT (std::memcmp (&back, &plan.recipe, sizeof (FillRecipe)) == 0);
            GF_EXPECT (isValid (back));
        }
    }

    // Other algorithms are never touched.
    auto stutter = pitchPlan (Algo::Stutter);
    scale::decidePitch (stutter, ctx, s);
    GF_EXPECT (stutter.recipe.pitchSemis == 0);

    // A recipe replayed from Lock keeps its recorded pitch.
    auto locked = pitchPlan (Algo::PitchRise);
    locked.recipe.flags = FillRecipe::flagFromLock;
    locked.recipe.pitchSemis = 11;
    scale::decidePitch (locked, ctx, s);
    GF_EXPECT (locked.recipe.pitchSemis == 11);
}

GF_TEST (Scale, d_minor_pitch_rise_200_fills_are_all_in_key)
{
    GapContext ctx;
    auto s = settingsFor (2, (int) scale::ScaleType::NaturalMinor);
    const bool allowed[12] = { true, false, true, true, false, true, false, true, true, false, true, false };   // 0 2 3 5 7 8 10

    for (int i = 0; i < 200; ++i)
    {
        ctx.key = hashCombine (0xD1, (std::uint64_t) i);
        s.intensity01 = (float) (i % 10) / 9.0f;
        s.chaos01 = (float) ((i / 10) % 10) / 9.0f;

        auto plan = pitchPlan (Algo::PitchRise);
        scale::decidePitch (plan, ctx, s);
        GF_EXPECT_MSG (allowed[mod12 (plan.recipe.pitchSemis)], "rise " + juce::String ((int) plan.recipe.pitchSemis));
    }
}

GF_TEST (Scale, end_to_end_pitch_rise_recipes_are_in_key)
{
    const auto recipes = renderRecipes ({ { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 6.0f },
                                          { "scaleRoot", 2.0f }, { "scaleType", 2.0f }, { "scaleAmount", 100.0f } }, 512);
    GF_EXPECT (! recipes.empty());

    const auto set = scale::makeScaleSet (2, 2);

    for (const auto& r : recipes)
    {
        GF_EXPECT (r.algo == (std::uint8_t) Algo::PitchRise);
        GF_EXPECT (r.pitchSemis > 0);
        GF_EXPECT_MSG (set.containsPitchClass (2 + r.pitchSemis), "pitch " + juce::String ((int) r.pitchSemis));
    }
}

GF_TEST (Scale, granular_ratios_in_scale)
{
    constexpr float kRatios[] = { 0.25f, 0.5f, 0.594604f, 0.6674f, 0.75f, 0.8409f,
                                  1.0f, 1.1225f, 1.25f, 1.3348f, 1.5f, 1.6818f, 2.0f };

    for (int type = 1; type < scale::kNumScaleTypes; ++type)
        for (int root = 0; root < 12; ++root)
        {
            const auto set = scale::makeScaleSet (root, type);

            for (float r : kRatios)
            {
                const auto snapped = scale::snapRatio (r, set);
                GF_EXPECT (snapped >= 0.25f - 1.0e-6f && snapped <= 2.0f + 1.0e-6f);

                const auto k = 12.0 * std::log2 ((double) snapped);
                const auto ki = (int) std::lround (k);
                GF_EXPECT (std::abs (k - (double) ki) < 1.0e-4);                 // exactly 2^(k/12)
                GF_EXPECT (set.containsPitchClass (root + ki) || ki % 12 == 0);
            }
        }

    // Voice level: a Granular voice with the lock on renders finite, bounded audio.
    VoiceRig rig;
    FillVoice v;
    v.prepare (kRigSr, 2);
    auto s = rig.setup (Algo::Granular, 250.0);
    s.intensity01 = 0.9f;
    s.chaos01 = 0.9f;
    s.scaleLock = settingsFor (2, (int) scale::ScaleType::NaturalMinor).scaleLock;
    bool started = false;
    const auto out = rig.render (v, s, 12000, 64, &started);
    GF_EXPECT (started);
    GF_EXPECT (rmsOf (out, 0, 12000) > 0.001);

    for (int i = 0; i < out.getNumSamples(); ++i)
        GF_EXPECT (std::isfinite (out.getSample (0, i)) && std::abs (out.getSample (0, i)) < 4.0f);
}

GF_TEST (Scale, comb_frequency_in_scale_within_1_cent)
{
    juce::Random rnd (42);

    for (int type = 1; type < scale::kNumScaleTypes; ++type)
        for (int root = 0; root < 12; root += 3)
        {
            const auto set = scale::makeScaleSet (root, type);

            for (int n = 0; n < 60; ++n)
            {
                const float hz = 55.0f * std::pow (2.0f, rnd.nextFloat() * 5.0f);
                const auto snapped = scale::snapFrequency (hz, set);
                const auto midi = 69.0 + 12.0 * std::log2 ((double) snapped / 440.0);
                const auto nearest = std::lround (midi);

                GF_EXPECT (std::abs (midi - (double) nearest) < 0.01);          // < 1 cent from an equal-tempered note
                GF_EXPECT (set.containsPitchClass ((int) (((nearest % 12) + 12) % 12)));

                // And no scale degree is closer than the one chosen (ties go upward).
                const auto raw = 69.0 + 12.0 * std::log2 ((double) hz / 440.0);

                for (int d = -12; d <= 12; ++d)
                {
                    const auto cand = (int) std::lround (raw) + d;

                    if (set.containsPitchClass (((cand % 12) + 12) % 12))
                        GF_EXPECT (std::abs (raw - (double) nearest) <= std::abs (raw - (double) cand) + 1.0e-9);
                }
            }
        }

    // A pitch already on a degree is returned (within float precision) unchanged.
    const auto dMinor = scale::makeScaleSet (2, 2);
    GF_EXPECT_NEAR (scale::snapFrequency (440.0f, dMinor), 440.0, 0.01);          // A is the fifth of D minor
    GF_EXPECT_NEAR (scale::snapFrequency (110.0f * std::pow (2.0f, 4.0f / 12.0f), dMinor), 110.0 * std::pow (2.0, 5.0 / 12.0), 0.05);

    // Bad input passes through.
    GF_EXPECT (scale::snapFrequency (0.0f, dMinor) == 0.0f);
    GF_EXPECT (scale::snapFrequency (-5.0f, dMinor) == -5.0f);
    GF_EXPECT (std::isnan (scale::snapFrequency (std::nanf (""), dMinor)));
}

GF_TEST (Scale, cloud_pitch_is_quantised_through_the_voice)
{
    VoiceRig rig;
    FillVoice v;
    v.prepare (kRigSr, 2);
    auto s = rig.setup (Algo::Granular, 250.0);
    s.modes.cloud = true;
    s.fxStart.settings.cloud.amount01 = 1.0f;
    s.fxStart.settings.cloud.pitchCents = 700.0f;
    s.scaleLock = settingsFor (0, (int) scale::ScaleType::MajorPentatonic).scaleLock;

    bool started = false;
    rig.render (v, s, 4000, 64, &started);
    GF_EXPECT (started);
    GF_EXPECT (v.getCloudMode().isActive());

    for (int i = 0; i < 400; ++i)
    {
        const auto g = v.getCloudMode().drawGrain (0);
        const auto cents = 1200.0 * std::log2 (std::abs (g.rate));
        const auto semis = (int) std::lround (cents / 100.0);
        GF_EXPECT (std::abs (cents - 100.0 * semis) < 0.5);                       // whole semitones
        GF_EXPECT (scale::makeScaleSet (0, (int) scale::ScaleType::MajorPentatonic).containsPitchClass (semis) || semis % 12 == 0);
    }
}

GF_TEST (Scale, block_size_invariant)
{
    const auto params = std::initializer_list<std::pair<const char*, float>> {
        { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 6.0f },
        { "scaleRoot", 9.0f }, { "scaleType", 11.0f }, { "scaleAmount", 60.0f } };

    const auto a = renderRecipes (params, 128);
    const auto b = renderRecipes (params, 1024);
    GF_EXPECT (! a.empty());
    GF_EXPECT (a.size() == b.size());

    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        GF_EXPECT (std::memcmp (&a[i], &b[i], sizeof (FillRecipe)) == 0);
}

GF_TEST (Scale, state_roundtrip_and_old_state_off)
{
    GapFillAudioProcessor fresh;
    auto& vts = fresh.getValueTreeState();
    GF_EXPECT (pi (vts.getRawParameterValue ("scaleType")) == 0);                 // a new / pre-N-MUS instance has the lock off
    GF_EXPECT (pi (vts.getRawParameterValue ("scaleRoot")) == 0);
    GF_EXPECT_NEAR (pv (vts.getRawParameterValue ("scaleAmount")), 100.0, 1.0e-6);

    GapFillAudioProcessor a;
    setParams (a, { { "scaleRoot", 7.0f }, { "scaleType", 5.0f }, { "scaleAmount", 42.0f } });
    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    GapFillAudioProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    GF_EXPECT (pi (b.getValueTreeState().getRawParameterValue ("scaleRoot")) == 7);
    GF_EXPECT (pi (b.getValueTreeState().getRawParameterValue ("scaleType")) == 5);
    GF_EXPECT_NEAR (pv (b.getValueTreeState().getRawParameterValue ("scaleAmount")), 42.0, 0.01);

    // Not part of Random / Random All / Surprise.
    const auto random = randomisableParameterIDs();

    for (const char* id : { "scaleRoot", "scaleType", "scaleAmount" })
        GF_EXPECT (! random.contains (id));
}

GF_TEST (Scale, pitch_context_override)
{
    const auto cMajor = scale::makeScaleSet (0, (int) scale::ScaleType::Major);
    scale::PitchContext fromD;
    fromD.sourcePc = 2;                                 // the hit is a D, not the tonic
    fromD.conf = 0.9f;

    // Default context: the user's tonic. +4 (E) is in C major.
    GF_EXPECT (scale::snapInterval (4, cMajor) == 4);
    // From D, +4 would be F# (not in C major): nearest in-key intervals are +3 (F) and +5 (G), tie -> smaller |i|.
    GF_EXPECT (scale::snapInterval (4, cMajor, fromD) == 3);
    // Every result lands in the scale for the supplied pitch class.
    for (int i = -24; i <= 24; ++i)
    {
        const auto r = scale::snapInterval (i, cMajor, fromD);
        GF_EXPECT (cMajor.containsPitchClass (2 + r) || r % 12 == 0);
    }

    // Out-of-range pitch class wraps instead of crashing.
    scale::PitchContext odd; odd.sourcePc = 14;
    GF_EXPECT (scale::snapInterval (4, cMajor, odd) == scale::snapInterval (4, cMajor, fromD));
}

GF_TEST (Scale, tables_names_and_descriptions)
{
    GF_EXPECT (scale::kNumScaleTypes == 13);

    for (int t = 1; t < scale::kNumScaleTypes; ++t)
    {
        const auto& tab = scale::scaleTable (t);
        GF_EXPECT (tab.count >= 5 && tab.count <= 7 && tab.degrees[0] == 0);

        for (int i = 1; i < tab.count; ++i)
            GF_EXPECT (tab.degrees[i] > tab.degrees[i - 1] && tab.degrees[i] < 12);
    }

    GF_EXPECT (scale::scaleTable (5).count == 7 && scale::scaleTable (5).degrees[3] == 6);      // Lydian #4
    GF_EXPECT (scale::scaleTable (11).count == 6);                                                // Blues
    GF_EXPECT (! scale::makeScaleSet (3, 0).active());
    GF_EXPECT (scale::makeScaleSet (99, 99).root == 11 && scale::makeScaleSet (-3, -1).type == 0);

    char label[64], classes[64];
    GF_EXPECT (juce::String (scale::describeShort (scale::makeScaleSet (2, 2), label, 64)) == "D Natural Minor");
    GF_EXPECT (juce::String (scale::describePitchClasses (scale::makeScaleSet (2, 2), classes, 64)) == "D E F G A A# C");
    GF_EXPECT (juce::String (scale::describeShort (scale::makeScaleSet (2, 0), label, 64)).isEmpty());
    GF_EXPECT (juce::String (scale::describePitchClasses (scale::makeScaleSet (2, 2), classes, 4)).length() < 4);
}

GF_TEST (Scale, audio_thread_does_not_allocate_with_the_lock_on)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 6.0f },
                       { "scaleRoot", 2.0f }, { "scaleType", 2.0f }, { "scaleAmount", 70.0f } });
    TestPlayHead ph;
    ph.bpm = goldenBpm;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, sceneAudio(), 512, goldenSampleRate, o);          // warm up

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, 512);
    block.clear();

    AllocGuard guard;

    for (int i = 0; i < 200; ++i)
        proc.processBlock (block, midi);

    GF_EXPECT (guard.count() == 0);
}
