// N-FX2 Granular Cloud (docs/dev/12 section 4).

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"
#include "../support/VoiceRig.h"

#include "features/cloud/CloudMode.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    VoiceSetup cloudSetup (const VoiceRig& rig, double ms, fx::CloudSettings c = {}, std::uint64_t seed = 5)
    {
        auto s = rig.setup (Algo::Granular, ms, seed);
        s.modes.cloud = true;
        c.amount01 = 1.0f;
        s.fxStart.settings.cloud = c;
        return s;
    }

    fx::CloudMode makeCloud (const fx::CloudSettings& c, std::int64_t anchor = 200000, float chaos = 0.0f,
                             std::uint64_t seed = 1)
    {
        fx::CloudMode m;
        m.configure (true, c, chaos, anchor, seed, kSr);
        return m;
    }
}

//==============================================================================
GF_TEST (Cloud, cloud_off_identity)
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

    const auto a = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 4.0f }, { "complexity", 70.0f } });
    const auto b = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 4.0f }, { "complexity", 70.0f },
                             { "cloudAmount", 0.0f }, { "cloudDensity", 80.0f }, { "cloudSize", 250.0f },
                             { "cloudSpan", 2000.0f }, { "cloudJitter", 100.0f }, { "cloudPitch", 1200.0f } });
    GF_EXPECT (buffersIdentical (a, b));
}

GF_TEST (Cloud, flag_uses_its_own_sub_rng_and_only_granular_voices_get_it)
{
    EngineSettings s;
    GapContext ctx;
    int on0 = 0, on50 = 0, on100 = 0, other = 0;
    const int trials = 4000;

    for (int i = 0; i < trials; ++i)
    {
        ctx.key = hashCombine (0xABCD, (std::uint64_t) i);
        FillPlan plan;
        plan.algoIndex = (int) Algo::Granular;
        plan.recipe.algo = (std::uint8_t) Algo::Granular;
        plan.recipe.seed = (std::uint32_t) i;
        const FillRecipe before = plan.recipe;

        s.fx.cloud.amount01 = 0.0f;
        fx::decideRenderModes (plan, ctx, s);
        on0 += plan.recipe.flags != 0;

        s.fx.cloud.amount01 = 0.5f;
        fx::decideRenderModes (plan, ctx, s);
        on50 += (plan.recipe.flags & FillRecipe::flagCloud) != 0;

        plan.recipe.flags = 0;
        s.fx.cloud.amount01 = 1.0f;
        fx::decideRenderModes (plan, ctx, s);
        on100 += (plan.recipe.flags & FillRecipe::flagCloud) != 0;

        auto after = plan.recipe;
        after.flags = 0;
        GF_EXPECT (std::memcmp (&after, &before, sizeof (FillRecipe)) == 0);

        FillPlan reverse = plan;
        reverse.recipe.flags = 0;
        reverse.algoIndex = (int) Algo::Reverse;
        s.fx.swell.amount01 = 0.0f;
        fx::decideRenderModes (reverse, ctx, s);
        other += (reverse.recipe.flags & FillRecipe::flagCloud) != 0;
    }

    GF_EXPECT (on0 == 0 && on100 == trials && other == 0);
    GF_EXPECT_MSG (std::abs (on50 - trials / 2) < trials / 20, "probability off: " + juce::String (on50));
}

GF_TEST (Cloud, grain_count_never_exceeds_48)
{
    VoiceRig rig;

    for (const auto& c : { fx::CloudSettings { 1.0f, 80.0f, 250.0f, 600.0f, 0.4f, 100.0f, 0.6f },
                           fx::CloudSettings { 1.0f, 1000.0f, 250.0f, 600.0f, 0.4f, 100.0f, 0.6f },   // absurd, must be limited
                           fx::CloudSettings { 1.0f, 80.0f, 10.0f, 100.0f, 1.0f, 1200.0f, 1.0f } })
    {
        FillVoice voice;
        voice.prepare (kSr, 2);
        const auto s = cloudSetup (rig, 1500.0, c);
        rig.spectral.release();
        GF_EXPECT (voice.start (s, rig.history, &rig.spectral));

        juce::AudioBuffer<float> wet (2, 64);
        int worst = 0;

        for (int block = 0; block < 900; ++block)
        {
            wet.clear();
            voice.render (wet, 0, 64, rig.history);
            worst = std::max (worst, voice.getCloudMode().activeGrains());
        }

        GF_EXPECT_MSG (worst <= fx::kCloudMaxGrains, "grains " + juce::String (worst));
        GF_EXPECT (worst > 0);
    }
}

GF_TEST (Cloud, density_limit_is_reported)
{
    const auto fine = fx::limitCloudDensity (80.0f, 250.0f);        // 20 simultaneous grains: within the limit
    GF_EXPECT (! fine.limited);
    GF_EXPECT_NEAR (fine.effective, 80.0f, 1.0e-4);

    const auto limited = fx::limitCloudDensity (200.0f, 250.0f);
    GF_EXPECT (limited.limited);
    GF_EXPECT_NEAR (limited.overlap, 40.0f, 1.0e-4);
    GF_EXPECT_NEAR (limited.effective, 160.0f, 1.0e-3);

    fx::FxMeters meters;
    EngineSettings s;
    s.fx.cloud.densityHz = 500.0f;
    s.fx.cloud.sizeMs = 250.0f;
    GapContext ctx;
    FillPlan plan;
    plan.algoIndex = (int) Algo::Granular;
    plan.recipe.flags = FillRecipe::flagCloud;
    VoiceSetup setup;
    setup.algo = Algo::Granular;
    setup.lengthSamples = 20000;
    fx::prepareVoiceSetup (setup, plan, ctx, s, &meters);
    GF_EXPECT (setup.modes.cloud);
    GF_EXPECT (meters.cloudLimited.load());
    GF_EXPECT (meters.cloudGrains.load() == 40);
}

GF_TEST (Cloud, positions_stay_within_the_span_and_the_available_history)
{
    fx::CloudSettings c;
    c.spanMs = 600.0f;
    c.sizeMs = 60.0f;
    c.jitter01 = 1.0f;
    c.pitchCents = 300.0f;
    const std::int64_t anchor = 150000;
    auto cloud = makeCloud (c, anchor, 0.5f, 42);

    const auto span = 0.6 * kSr;
    const std::int64_t oldest = anchor - 40000;           // history starts inside the span

    for (int i = 0; i < 5000; ++i)
    {
        const auto g = cloud.drawGrain (oldest);
        const auto len = (double) g.dur * std::abs (g.rate);
        const auto lo = g.rate > 0 ? g.pos : g.pos - len;
        const auto hi = g.rate > 0 ? g.pos + len : g.pos;

        GF_EXPECT (lo >= (double) oldest - 1.0e-6);               // never before history start
        GF_EXPECT (hi <= (double) anchor + 1.0e-6);               // never beyond the anchor
        GF_EXPECT (lo >= (double) anchor - span - len - 1.0);     // within the span (plus its own length)
    }
}

GF_TEST (Cloud, jitter_0_takes_every_grain_from_the_centre_of_the_span)
{
    fx::CloudSettings c;
    c.jitter01 = 0.0f;
    c.pitchCents = 0.0f;
    c.spanMs = 800.0f;
    c.sizeMs = 50.0f;
    const std::int64_t anchor = 300000;
    auto cloud = makeCloud (c, anchor, 0.0f, 9);

    for (int i = 0; i < 500; ++i)
    {
        const auto g = cloud.drawGrain (0);
        GF_EXPECT_NEAR (g.pos, (double) anchor - 0.4 * kSr, 1.0e-6);
        GF_EXPECT_NEAR (g.rate, 1.0, 1.0e-12);
    }

    // Jitter 100: spread over the whole span.
    c.jitter01 = 1.0f;
    auto wide = makeCloud (c, anchor, 0.0f, 9);
    double lo = 1.0e12, hi = -1.0e12;

    for (int i = 0; i < 3000; ++i)
    {
        const auto g = wide.drawGrain (0);
        lo = std::min (lo, g.pos);
        hi = std::max (hi, g.pos);
    }

    GF_EXPECT (hi - lo > 0.6 * 0.8 * kSr);
}

GF_TEST (Cloud, pitch_spread_stays_inside_the_cents_bounds_and_is_triangular)
{
    fx::CloudSettings c;
    c.pitchCents = 400.0f;
    c.jitter01 = 0.0f;
    auto cloud = makeCloud (c, 300000, 0.0f, 3);

    const int n = 20000;
    int inner = 0;
    double sum = 0.0;
    double worst = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto g = cloud.drawGrain (0);
        const auto cents = 1200.0 * std::log2 (g.rate);
        worst = std::max (worst, std::abs (cents));
        inner += std::abs (cents) <= 200.0;
        sum += cents;
    }

    GF_EXPECT (worst <= 400.0 + 1.0e-6);
    GF_EXPECT_NEAR ((double) inner / n, 0.75, 0.03);        // triangular: P(|x| <= c/2) = 3/4
    GF_EXPECT (std::abs (sum / n) < 8.0);

    // Zero spread = no shift at all.
    c.pitchCents = 0.0f;
    auto none = makeCloud (c, 300000, 0.0f, 3);

    for (int i = 0; i < 200; ++i)
        GF_EXPECT_NEAR (none.drawGrain (0).rate, 1.0, 1.0e-12);
}

GF_TEST (Cloud, scale_lock_hook_replaces_the_shift_with_an_in_scale_interval)
{
    fx::CloudSettings c;
    c.pitchCents = 1200.0f;
    c.jitter01 = 0.0f;
    auto cloud = makeCloud (c, 300000, 0.0f, 17);

    // Stand-in for N-MUS: snap to whole tones.
    cloud.setPitchQuantiser ([] (float cents, void*) { return std::round (cents / 200.0f) * 200.0f; }, nullptr);

    std::map<int, int> seen;

    for (int i = 0; i < 3000; ++i)
    {
        const auto g = cloud.drawGrain (0);
        const auto cents = 1200.0 * std::log2 (g.rate);
        GF_EXPECT_NEAR (std::fmod (std::abs (cents) + 100.0, 200.0), 100.0, 1.0e-6);
        ++seen[(int) std::lround (cents / 200.0)];
    }

    GF_EXPECT (seen.size() >= 5);
}

GF_TEST (Cloud, output_is_finite_and_comparable_in_level_across_dense_settings)
{
    VoiceRig rig;
    std::vector<double> levels;

    for (float density : { 20.0f, 40.0f, 80.0f })
    {
        for (float size : { 60.0f, 120.0f, 250.0f })
        {
            for (float pitch : { 0.0f, 600.0f })
            {
                fx::CloudSettings c;
                c.densityHz = density;
                c.sizeMs = size;
                c.pitchCents = pitch;

                if (density * size * 0.001f < 1.5f)
                    continue;                           // sparse clouds are quiet by design

                FillVoice voice;
                voice.prepare (kSr, 2);
                const auto s = cloudSetup (rig, 1200.0, c);
                const auto wet = rig.render (voice, s, s.lengthSamples, 64);
                GF_EXPECT (allFinite (wet));
                GF_EXPECT (maxAbs (wet) < 4.0f);
                levels.push_back (rmsOf (wet, (int) (kSr * 0.3), (int) (kSr * 0.5)));
            }
        }
    }

    GF_EXPECT (levels.size() >= 6);
    const auto [lo, hi] = std::minmax_element (levels.begin(), levels.end());
    GF_EXPECT_MSG (20.0 * std::log10 (*hi / *lo) < 3.0, "level spread " + juce::String (20.0 * std::log10 (*hi / *lo)) + " dB");
}

GF_TEST (Cloud, same_seed_same_output_and_block_size_invariant)
{
    VoiceRig rig;
    const auto s = cloudSetup (rig, 600.0);
    juce::AudioBuffer<float> first;

    for (int block : { 1, 13, 32, 64, 1000 })
    {
        FillVoice voice;
        voice.prepare (kSr, 2);
        const auto wet = rig.render (voice, s, s.lengthSamples + 100, block);

        if (first.getNumSamples() == 0)
            first.makeCopyOf (wet);
        else
            GF_EXPECT_MSG (buffersIdentical (wet, first), "block " + juce::String (block));
    }

    auto other = s;
    other.rngSeed = s.rngSeed + 1;
    FillVoice voice;
    voice.prepare (kSr, 2);
    GF_EXPECT (! buffersIdentical (rig.render (voice, other, s.lengthSamples + 100, 64), first));
}

GF_TEST (Cloud, frozen_capture_source_ignores_audio_written_after_the_anchor)
{
    // A frozen (captured) history and a live one are identical up to the anchor;
    // the cloud may only read at or before the anchor, so what comes after
    // (the live input) cannot influence it.
    VoiceRig frozen (kSr, 3.0, 7);
    VoiceRig live (kSr, 3.0, 7);
    const auto anchor = frozen.anchor;

    juce::AudioBuffer<float> more (2, 24000);
    juce::Random rnd (99);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 24000; ++i)
            more.setSample (ch, i, rnd.nextFloat() - 0.5f);

    live.history.write (more, 0, 24000);

    FillVoice a, b;
    a.prepare (kSr, 2);
    b.prepare (kSr, 2);
    auto sa = cloudSetup (frozen, 500.0), sb = cloudSetup (live, 500.0);
    sa.sourceAnchor = sb.sourceAnchor = anchor;

    GF_EXPECT (buffersIdentical (frozen.render (a, sa, sa.lengthSamples, 64), live.render (b, sb, sb.lengthSamples, 64)));
}

GF_TEST (Cloud, ends_with_the_host_voice_before_the_next_protected_hit)
{
    VoiceRig rig;
    FillVoice plain, cloud;
    plain.prepare (kSr, 2);
    cloud.prepare (kSr, 2);

    auto sp = rig.setup (Algo::Granular, 300.0, 5);
    auto sc = cloudSetup (rig, 300.0);

    rig.render (plain, sp, 20000, 64);
    const auto wet = rig.render (cloud, sc, 20000, 64);

    GF_EXPECT (cloud.getLengthSamples() == plain.getLengthSamples());
    GF_EXPECT (lastAbove (wet, 0.0f) < sc.lengthSamples);
    GF_EXPECT (! cloud.isActive());
    GF_EXPECT (std::abs (wet.getSample (0, 0)) < 1.0e-3f);
}

GF_TEST (Cloud, no_alloc)
{
    VoiceRig rig;
    FillVoice voice;
    voice.prepare (kSr, 2);
    const auto s = cloudSetup (rig, 1500.0);
    rig.spectral.release();
    GF_EXPECT (voice.start (s, rig.history, &rig.spectral));

    juce::AudioBuffer<float> wet (2, 64);
    wet.clear();
    voice.render (wet, 0, 64, rig.history);        // warm up

    AllocGuard guard;

    for (int i = 0; i < 400; ++i)
    {
        wet.clear();
        voice.render (wet, 0, 64, rig.history);
    }

    GF_EXPECT (guard.count() == 0);
}

GF_TEST (Cloud, dense_clouds_are_smoother_than_plain_granular)
{
    VoiceRig rig;

    auto modulation = [&] (bool cloud, float density, float size)
    {
        FillVoice voice;
        voice.prepare (kSr, 2);
        fx::CloudSettings c;
        c.densityHz = density;
        c.sizeMs = size;
        auto s = cloud ? cloudSetup (rig, 1000.0, c) : rig.setup (Algo::Granular, 1000.0, 5);

        if (! cloud)
        {
            s.complexity01 = 0.4f;
            s.intensity01 = 0.5f;
        }

        const auto wet = rig.render (voice, s, s.lengthSamples, 64);
        const int win = (int) (kSr * 0.01);
        std::vector<double> env;

        for (int i = (int) (kSr * 0.15); i + win < (int) (kSr * 0.85); i += win)
            env.push_back (std::hypot (rmsOf (wet, i, win, 0), rmsOf (wet, i, win, 1)));      // both channels: pan moves energy, not loudness

        double mean = 0.0;

        for (auto e : env) mean += e;

        mean /= (double) env.size();
        double var = 0.0;

        for (auto e : env) var += (e - mean) * (e - mean);

        return std::sqrt (var / (double) env.size()) / std::max (1.0e-9, mean);
    };

    const auto plain = modulation (false, 0.0f, 0.0f);
    const auto atDefaults = modulation (true, 20.0f, 60.0f);
    const auto mid = modulation (true, 40.0f, 120.0f);
    const auto dense = modulation (true, 80.0f, 250.0f);
    std::cout << "         RMS-modulation depth: plain granular " << plain << ", cloud at the registry defaults (20/s, 60 ms) "
              << atDefaults << ", 40/s 120 ms " << mid << ", 80/s 250 ms " << dense << std::endl;
    GF_EXPECT_MSG (dense <= plain * 0.8, "cloud " + juce::String (dense) + " vs granular " + juce::String (plain));
}
