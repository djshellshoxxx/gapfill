// C-3 Controlled Mistakes (docs/dev/04-SPEC-CREATIVE.md C-3).

#include "../support/TestRig.h"

#include "dsp/FillVoice.h"
#include "features/mistakes/Mistakes.h"

#include <array>
#include <cmath>
#include <iostream>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    /** One voice rendered alone against a drum-loop history. */
    struct VoiceRun
    {
        juce::AudioBuffer<float> out;
        int activeSamples = 0;
        bool started = false;
    };

    VoiceRun renderVoice (Algo algo, int mistakeType, int lengthSamples = 14400, int channels = 2,
                          float amount = 1.0f, std::uint32_t seed = 12345, float chaos = 0.2f)
    {
        static SourceHistory history;
        static bool filled = false;

        if (! filled)
        {
            // A continuous, decorrelated stereo source (tone + noise) so every algorithm has material to play.
            history.prepare (2, (int) (6.0 * kSr));
            juce::AudioBuffer<float> src (2, (int) (3.0 * kSr));
            Rng noise;
            noise.seed (4242);

            for (int i = 0; i < src.getNumSamples(); ++i)
            {
                const auto tone = 0.35f * std::sin (2.0f * 3.14159265f * 196.0f * (float) i / (float) kSr);
                src.setSample (0, i, tone + 0.25f * noise.nextBipolar());
                src.setSample (1, i, 0.8f * tone + 0.25f * noise.nextBipolar());
            }

            history.write (src, 0, src.getNumSamples());
            filled = true;
        }

        auto voicePtr = std::make_unique<FillVoice>();
        auto& voice = *voicePtr;
        voice.prepare (kSr, channels);
        auto spectralPtr = std::make_unique<SpectralSmear>();
        auto& spectral = *spectralPtr;
        spectral.prepare (kSr, 2);

        VoiceSetup s;
        s.algo = algo;
        s.sampleRate = kSr;
        s.lengthSamples = lengthSamples;
        s.fadeInSamples = 96;
        s.fadeOutSamples = 960;
        s.amp = 0.8f;
        s.intensity01 = 0.6f;
        s.complexity01 = 0.5f;
        s.chaos01 = chaos;
        s.sourceAnchor = (std::int64_t) (1.5 * kSr);
        s.sinceEventSamples = 4800;
        s.stutterSeconds = 0.0625f;
        s.delaySeconds = 0.0625f;
        s.rngSeed = 777;
        s.mistakeType = mistakeType;
        s.mistakeAmt01 = amount;
        s.mistakeSeed = seed;

        VoiceRun run;
        run.out.setSize (2, lengthSamples + 4800);
        run.out.clear();
        run.started = voice.start (s, history, &spectral);

        if (! run.started)
            return run;

        int pos = 0;

        while (voice.isActive() && pos < run.out.getNumSamples())
        {
            const auto n = juce::jmin (512, run.out.getNumSamples() - pos);
            voice.render (run.out, pos, n, history);
            pos += n;
        }

        // Count the samples up to the last non-silent one (the voice deactivates itself).
        for (int i = run.out.getNumSamples() - 1; i >= 0; --i)
            if (std::abs (run.out.getSample (0, i)) > 0.0f || std::abs (run.out.getSample (1, i)) > 0.0f)
            {
                run.activeSamples = i + 1;
                break;
            }

        return run;
    }

    double rmsOf (const juce::AudioBuffer<float>& b, int start, int n)
    {
        double sum = 0.0;
        for (int i = start; i < start + n && i < b.getNumSamples(); ++i)
            sum += (double) b.getSample (0, i) * b.getSample (0, i);
        return std::sqrt (sum / std::max (1, n));
    }
}

GF_TEST (Mistakes, zero_is_bit_identical_and_touches_no_rng)
{
    mistakes::Roller zero (0.0f, 99);
    GF_EXPECT (! zero.hit());
    GF_EXPECT (zero.draws() == 0);

    GapFillAudioProcessor a, b;
    setParams (a, { { "fill", 85.0f } });
    setParams (b, { { "fill", 85.0f }, { "mistakes", 0.0f } });
    const auto ra = renderGoldenScenarioA (a);
    const auto rb = renderGoldenScenarioA (b);
    GF_EXPECT (ra.fills > 0);
    GF_EXPECT (compareBuffers (ra.out, rb.out, 0.0).maxAbs == 0.0);

    a.drainFillEvents();
    for (int i = 0; i < a.getRecipeHistory().size(); ++i)
        GF_EXPECT (a.getRecipeHistory().get (i).mistakeType == 0 && a.getRecipeHistory().get (i).mistakeAmt == 0);
}

GF_TEST (Mistakes, probability_converges_to_twenty_percent)
{
    for (auto [amount, expected] : { std::pair { 1.0f, 0.20 }, std::pair { 0.5f, 0.10 }, std::pair { 0.25f, 0.05 } })
    {
        int hits = 0;
        constexpr int n = 10000;

        for (int i = 0; i < n; ++i)
            hits += mistakes::Roller (amount, hashCombine (0x1234567ULL, (std::uint64_t) i)).hit() ? 1 : 0;

        GF_EXPECT_NEAR ((double) hits / n, expected, 0.02);
    }
}

GF_TEST (Mistakes, roll_returns_one_type_with_the_amount)
{
    mistakes::Facts f;
    f.algo = (int) Algo::Stutter;
    f.lengthSamples = 24000;
    f.sourceAvailable = 200000;
    f.repeats = 6;

    int seen[mistakes::NumTypes] {};

    for (int i = 0; i < 4000; ++i)
    {
        f.seed = (std::uint32_t) (i * 2654435761u);
        mistakes::Roller r (0.6f, (std::uint64_t) i);
        const auto roll = r.pick (f);
        GF_EXPECT (roll.type >= mistakes::Wobble && roll.type < mistakes::NumTypes);
        GF_EXPECT (roll.amount == 60);
        GF_EXPECT (r.draws() == 1);
        GF_EXPECT (! mistakes::skipped (roll.type, f));
        ++seen[roll.type];
    }

    for (int t = 1; t < mistakes::NumTypes; ++t)
        GF_EXPECT_MSG (seen[t] > 300, "type " + juce::String (t) + " seen " + juce::String (seen[t]));

    // No valid type at all -> no mistake.
    mistakes::Facts none;
    none.algo = (int) Algo::Texture;
    none.lengthSamples = 100;
    none.mono = true;
    GF_EXPECT (mistakes::Roller (1.0f, 1).pick (none).type == mistakes::Wobble);   // wobble applies to everything
}

GF_TEST (Mistakes, applicability_table_is_complete)
{
    // rows: Wobble, OctaveDisplace, MissedRepeat, Brake, StereoBounce; columns in Algo order.
    const std::array<std::array<bool, 15>, 6> expected =
    {{
        { },
        // Wobble
        { true, true, true, true, true, true, true, true, true, true, true, true, true, true, true },
        // OctaveDisplace: Reverse, Stutter, Granular, Freeze, PitchRise, PitchFall
        { true, true, false, true, true, true, true, false, false, false, false, false, false, false, false },
        // MissedRepeat: Stutter, Delay, Scramble
        { false, true, true, false, false, false, false, false, false, false, false, true, false, false, false },
        // Brake: Reverse, Stutter, Granular, Freeze
        { true, true, false, true, true, false, false, false, false, false, false, false, false, false, false },
        // StereoBounce: all
        { true, true, true, true, true, true, true, true, true, true, true, true, true, true, true },
    }};

    for (int type = 1; type <= 5; ++type)
        for (int algo = 0; algo < 15; ++algo)
            GF_EXPECT_MSG (mistakes::applicable (type, algo) == expected[(size_t) type][(size_t) algo],
                           "type " + juce::String (type) + " algo " + juce::String (algo));

    GF_EXPECT (! mistakes::applicable (0, 0) && ! mistakes::applicable (6, 0) && ! mistakes::applicable (1, 15) && ! mistakes::applicable (1, -1));
}

GF_TEST (Mistakes, skip_rules)
{
    mistakes::Facts f;
    f.algo = (int) Algo::Reverse;
    f.lengthSamples = 24000;               // 500 ms
    f.sampleRate = kSr;
    f.sourceAvailable = 1000000;
    f.repeats = 6;

    // Octave: find seeds for up and down.
    std::uint32_t up = 0, down = 0;
    for (std::uint32_t s = 1; s < 100 && (up == 0 || down == 0); ++s)
        (mistakes::octaveUp (s) ? up : down) = s;

    GF_EXPECT (up != 0 && down != 0);

    f.seed = up;
    GF_EXPECT (! mistakes::skipped (mistakes::OctaveDisplace, f));
    f.sourceAvailable = 2 * f.lengthSamples - 1;                       // x2 would read past the captured material
    GF_EXPECT (mistakes::skipped (mistakes::OctaveDisplace, f));
    f.seed = down;                                                      // x0.5 never needs more material
    GF_EXPECT (! mistakes::skipped (mistakes::OctaveDisplace, f));
    f.sourceAvailable = 0;
    GF_EXPECT (! mistakes::skipped (mistakes::OctaveDisplace, f));

    f.algo = (int) Algo::Granular; f.sourceAvailable = 1000000;
    f.seed = up;
    GF_EXPECT (mistakes::skipped (mistakes::OctaveDisplace, f));       // grains read ahead: only the octave down
    f.seed = down;
    GF_EXPECT (! mistakes::skipped (mistakes::OctaveDisplace, f));

    // Brake: fill shorter than 120 ms.
    f.algo = (int) Algo::Reverse;
    f.lengthSamples = (std::int64_t) (0.119 * kSr);
    GF_EXPECT (mistakes::skipped (mistakes::Brake, f));
    f.lengthSamples = (std::int64_t) (0.121 * kSr);
    GF_EXPECT (! mistakes::skipped (mistakes::Brake, f));

    // Missed repeat: fewer than 3 repeats.
    f.algo = (int) Algo::Stutter;
    f.repeats = 2;
    GF_EXPECT (mistakes::skipped (mistakes::MissedRepeat, f));
    f.repeats = 3;
    GF_EXPECT (! mistakes::skipped (mistakes::MissedRepeat, f));

    // Stereo bounce: mono, coherent capture, or shorter than 80 ms.
    f.lengthSamples = (std::int64_t) (0.2 * kSr);
    GF_EXPECT (! mistakes::skipped (mistakes::StereoBounce, f));
    f.mono = true;
    GF_EXPECT (mistakes::skipped (mistakes::StereoBounce, f));
    f.mono = false; f.stereoCoherent = true;
    GF_EXPECT (mistakes::skipped (mistakes::StereoBounce, f));
    f.stereoCoherent = false; f.lengthSamples = (std::int64_t) (0.079 * kSr);
    GF_EXPECT (mistakes::skipped (mistakes::StereoBounce, f));
}

GF_TEST (Mistakes, every_type_on_every_algorithm_is_finite_bounded_and_not_longer)
{
    const auto planned = 14400;

    for (int a = 0; a < numAlgos; ++a)
    {
        const auto base = renderVoice ((Algo) a, 0, planned);
        GF_EXPECT_MSG (base.started, juce::String (algoName ((Algo) a)));
        const auto baseActive = base.activeSamples;

        for (int type = 1; type <= 5; ++type)
        {
            if (! mistakes::applicable (type, a))
                continue;

            for (std::uint32_t seed : { 1u, 2u, 3u, 4u })
            {
                const auto run = renderVoice ((Algo) a, type, planned, 2, 1.0f, seed);
                const auto msg = juce::String (algoName ((Algo) a)) + " type " + juce::String (type) + " seed " + juce::String ((int) seed);
                GF_EXPECT_MSG (run.started, msg);
                GF_EXPECT_MSG (allFinite (run.out), msg);
                GF_EXPECT_MSG (maxAbs (run.out, 0, run.out.getNumSamples()) < 8.0f, msg);
                GF_EXPECT_MSG (run.activeSamples <= planned, msg + " ran past the planned length (" + juce::String (run.activeSamples) + ")");
                GF_EXPECT_MSG (run.activeSamples <= std::max (baseActive, planned), msg);
            }
        }
    }
}

GF_TEST (Mistakes, no_click_for_any_type)
{
    // The mistake must not add discontinuities beyond what the plain fill already has.
    float worstExcess = -1.0e9f;

    struct Report { float& v; ~Report() { std::cout << "         worst relative-jump excess over the plain fill: " << v << std::endl; } } report { worstExcess };

    for (Algo algo : { Algo::Reverse, Algo::Stutter, Algo::Delay, Algo::Granular, Algo::Freeze, Algo::Scramble, Algo::PitchRise })
    {
        const auto base = renderVoice (algo, 0, 14400);
        const auto baseJump = maxRelativeJump (base.out);

        for (int type = 1; type <= 5; ++type)
        {
            if (! mistakes::applicable (type, (int) algo))
                continue;

            for (std::uint32_t seed : { 5u, 6u, 7u })
            {
                const auto run = renderVoice (algo, type, 14400, 2, 1.0f, seed);
                const auto jump = maxRelativeJump (run.out);
                worstExcess = std::max (worstExcess, jump - baseJump);
                GF_EXPECT_MSG (jump <= baseJump + 1.5f,
                               juce::String (algoName (algo)) + " type " + juce::String (type) + " jump " + juce::String (jump)
                                   + " vs plain " + juce::String (baseJump));
            }
        }
    }
}

GF_TEST (Mistakes, missed_repeat_drops_exactly_one_stutter_slice_and_keeps_the_first)
{
    const auto base = renderVoice (Algo::Stutter, 0, 24000);

    for (std::uint32_t seed : { 11u, 12u, 13u, 14u, 15u, 16u })
    {
        const auto run = renderVoice (Algo::Stutter, mistakes::MissedRepeat, 24000, 2, 1.0f, seed);
        const int slice = (int) (0.0625f * kSr);        // 3000 samples
        int dropped = 0;

        for (int k = 0; k * slice + slice <= 24000 - 3000; ++k)
        {
            const auto plain = rmsOf (base.out, k * slice + slice / 4, slice / 2);
            const auto got = rmsOf (run.out, k * slice + slice / 4, slice / 2);


            if (plain > 0.01 && got < plain * 0.05)
            {
                ++dropped;
                GF_EXPECT_MSG (k >= 1, "the first repeat must be kept");
            }
        }

        GF_EXPECT_MSG (dropped == 1, "seed " + juce::String ((int) seed) + " dropped " + juce::String (dropped) + " slices");
    }
}

GF_TEST (Mistakes, brake_slows_and_fades_the_last_quarter_only)
{
    mistakes::MistakeStage stage;
    mistakes::StageConfig c;
    c.type = mistakes::Brake; c.algo = (int) Algo::Reverse; c.lengthSamples = 48000; c.amount01 = 1.0f;
    stage.start (c);
    GF_EXPECT (stage.isActive());

    GF_EXPECT_NEAR (stage.rateMultiplier (0.0f), 1.0, 1.0e-6);
    GF_EXPECT_NEAR (stage.rateMultiplier (0.5f), 1.0, 1.0e-6);
    GF_EXPECT_NEAR (stage.rateMultiplier (0.75f), 1.0, 1.0e-6);
    GF_EXPECT_NEAR (stage.rateMultiplier (0.875f), 0.575, 1.0e-5);
    GF_EXPECT_NEAR (stage.rateMultiplier (1.0f), 0.15, 1.0e-6);

    float prev = 2.0f;
    for (float t = 0.0f; t <= 1.0f; t += 0.01f)
    {
        const auto g = stage.rateMultiplier (t);
        GF_EXPECT (g <= prev + 1.0e-6f && g >= 0.15f - 1.0e-6f);
        prev = g;
    }

    // Octave multipliers.
    mistakes::MistakeStage oct;
    mistakes::StageConfig o;
    o.type = mistakes::OctaveDisplace; o.algo = (int) Algo::Stutter; o.lengthSamples = 1000; o.amount01 = 1.0f;
    std::uint32_t up = 0, down = 0;
    for (std::uint32_t s = 1; s < 100 && (up == 0 || down == 0); ++s)
        (mistakes::octaveUp (s) ? up : down) = s;

    o.seed = up;   oct.start (o);   GF_EXPECT_NEAR (oct.rateMultiplier (0.3f), 2.0, 1.0e-9);
    o.seed = down; oct.start (o);   GF_EXPECT_NEAR (oct.rateMultiplier (0.3f), 0.5, 1.0e-9);
    o.algo = (int) Algo::Granular; o.seed = up; oct.start (o);
    GF_EXPECT (! oct.isActive());          // Granular never goes up
    o.algo = (int) Algo::Delay; o.seed = down; oct.start (o);
    GF_EXPECT (! oct.isActive());          // not applicable to Delay
}

GF_TEST (Mistakes, wobble_stays_within_three_percent_of_the_rate)
{
    mistakes::MistakeStage stage;
    mistakes::StageConfig c;
    c.type = mistakes::Wobble; c.algo = (int) Algo::Texture; c.lengthSamples = 96000; c.amount01 = 1.0f; c.sampleRate = kSr;

    for (std::uint32_t seed : { 1u, 2u, 3u, 4u, 5u })
    {
        c.seed = seed;
        stage.start (c);
        GF_EXPECT (stage.isActive());

        // 2 kHz sine; measure the period between rising zero crossings after the delay line has filled.
        const double f0 = 2000.0;
        std::vector<float> out (96000);

        for (int i = 0; i < 96000; ++i)
        {
            float l = (float) std::sin (2.0 * 3.14159265358979 * f0 * i / kSr), r = l;
            stage.process (l, r);
            out[(size_t) i] = l;
        }

        std::vector<double> crossings;

        for (size_t i = 2000; i + 1 < out.size(); ++i)
            if (out[i] <= 0.0f && out[i + 1] > 0.0f)
                crossings.push_back ((double) i + (-out[i]) / (out[i + 1] - out[i]));

        GF_EXPECT (crossings.size() > 100);
        double worst = 0.0;

        for (size_t i = 8; i < crossings.size(); ++i)
        {
            const auto period = (crossings[i] - crossings[i - 8]) / 8.0;     // average over 8 cycles
            worst = std::max (worst, std::abs (kSr / f0 / period - 1.0));
        }

        GF_EXPECT_MSG (worst <= 0.035, "seed " + juce::String ((int) seed) + " rate deviation " + juce::String (worst));
        GF_EXPECT_MSG (worst > 0.002, "wobble had no audible depth for seed " + juce::String ((int) seed));
    }
}

GF_TEST (Mistakes, bounce_is_never_applied_to_mono_voices)
{
    const auto stereo = renderVoice (Algo::Reverse, 0, 14400, 2);
    const auto monoPlain = renderVoice (Algo::Reverse, 0, 14400, 1);
    const auto monoBounce = renderVoice (Algo::Reverse, mistakes::StereoBounce, 14400, 1, 1.0f, 99);
    const auto bounced = renderVoice (Algo::Reverse, mistakes::StereoBounce, 14400, 2, 1.0f, 99);

    GF_EXPECT (rmsOf (monoPlain.out, 0, 14400) > 0.01);                                 // the voice really sounds
    GF_EXPECT (compareBuffers (monoPlain.out, monoBounce.out, 0.0).maxAbs == 0.0);      // mono stays mono, untouched
    GF_EXPECT (compareBuffers (stereo.out, bounced.out, 0.0).maxAbs > 0.0);             // stereo is changed
}

GF_TEST (Mistakes, plugin_level_decisions_are_unchanged_and_rolls_are_deterministic)
{
    GapFillAudioProcessor off, on, again;
    const std::initializer_list<std::pair<const char*, float>> base { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 30.0f } };
    setParams (off, base);
    setParams (on, base);
    setParams (again, base);
    setParams (on, { { "mistakes", 100.0f } });
    setParams (again, { { "mistakes", 100.0f } });

    auto starts = [] (GapFillAudioProcessor& p)
    {
        std::vector<std::tuple<std::int64_t, int, int>> v;
        RenderOptions o;
        o.onBlock = [&] (int, int)
        {
            const auto n = p.drainFillEvents();

            for (int i = 0; i < n; ++i)
            {
                const auto& e = p.getGapOutcomeLog().get (i);
                if (e.type == FillEventType::Started)
                    v.emplace_back (e.pos, (int) e.recipe.lengthMs, (int) e.recipe.mistakeType);
            }
        };
        renderOffline (p, drumLoop (kSr, 150.0, 16), 512, kSr, o);
        return v;
    };

    const auto a = starts (off);
    const auto b = starts (on);
    const auto c = starts (again);

    GF_EXPECT (a.size() > 50 && a.size() == b.size());

    int typed = 0;

    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        GF_EXPECT (std::get<0> (a[i]) == std::get<0> (b[i]));            // same gaps are filled ...
        GF_EXPECT (std::get<1> (a[i]) == std::get<1> (b[i]));            // ... with the same length: never extended
        GF_EXPECT (std::get<2> (a[i]) == 0);
        GF_EXPECT (std::get<2> (b[i]) <= mistakes::NumTypes - 1);
        typed += std::get<2> (b[i]) != 0 ? 1 : 0;
    }

    std::cout << "         " << typed << " of " << b.size() << " fills carried a mistake" << std::endl;
    GF_EXPECT (typed > 0 && typed < (int) b.size() / 2);                 // ~20 % at most
    GF_EXPECT (b == c);                                                  // same seed + same positions -> same mistakes
}

GF_TEST (Mistakes, plugin_output_with_mistakes_is_finite_bounded_and_repeatable)
{
    GapFillAudioProcessor a, b;
    const std::initializer_list<std::pair<const char*, float>> p { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 30.0f },
                                                                      { "mistakes", 100.0f }, { "personality", 8.0f }, { "chaos", 80.0f } };
    setParams (a, p);
    setParams (b, p);
    const auto ra = renderGoldenScenarioA (a);
    const auto rb = renderGoldenScenarioA (b);
    GF_EXPECT (ra.fills > 0);
    GF_EXPECT (compareBuffers (ra.out, rb.out, 0.0).maxAbs == 0.0);
    GF_EXPECT (allFinite (ra.out) && maxAbs (ra.out, 0, ra.out.getNumSamples()) < 8.0f);
}

GF_TEST (Mistakes, parameter_is_performance_state)
{
    GF_EXPECT (performanceParameterIDs().contains ("mistakes"));
    GF_EXPECT (! randomisableParameterIDs().contains ("mistakes"));
}
