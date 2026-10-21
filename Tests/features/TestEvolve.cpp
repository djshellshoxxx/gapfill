// C-8 Evolve (docs/dev/04-SPEC-CREATIVE.md C-8, doc 12 section 9).

#include "../support/TestRig.h"

#include "features/evolve/Evolve.h"

#include <cmath>
#include <cstring>
#include <set>

using namespace gapfill;
using namespace gftest;

namespace
{
    Effective::TransportInfo transport (double ppq = 0.0, int num = 4, int den = 4, bool valid = true)
    {
        return { valid, ppq, num, den, 120.0 };
    }

    EngineSettings evolving (int period = 8, int seed = 7)
    {
        EngineSettings s;
        s.seed = seed;
        s.perf.evolve.periodBars = period;
        for (auto& w : s.weights) w = 0.5f;
        return s;
    }

    std::vector<std::int64_t> startsFor (int block, std::initializer_list<std::pair<const char*, float>> params, int bars = 8)
    {
        GapFillAudioProcessor proc;
        setParams (proc, params);
        TestPlayHead ph;
        ph.bpm = goldenBpm; ph.tsNum = 4; ph.tsDen = 4;
        RenderOptions o;
        o.playHead = &ph;
        renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, bars), block, goldenSampleRate, o);
        proc.drainFillEvents();

        std::vector<std::int64_t> starts;

        for (int i = proc.getGapOutcomeLog().size() - 1; i >= 0; --i)
            if (proc.getGapOutcomeLog().get (i).type == FillEventType::Started)
                starts.push_back (proc.getGapOutcomeLog().get (i).pos);

        return starts;
    }
}

GF_TEST (Evolve, off_is_identity)
{
    auto s = evolving (0);
    EngineSettings before;
    std::memcpy (&before, &s, sizeof (s));
    evolve::apply (s, 17.3, transport (17.3));
    GF_EXPECT (std::memcmp (&before, &s, sizeof (s)) == 0);

    GapFillAudioProcessor a, b;
    setParams (a, { { "fill", 80.0f } });
    setParams (b, { { "fill", 80.0f }, { "evolve", 0.0f } });
    GF_EXPECT (compareBuffers (renderGoldenScenarioA (a).out, renderGoldenScenarioA (b).out, 0.0).maxAbs == 0.0);
}

GF_TEST (Evolve, profile_sequence_has_no_immediate_repeat_and_covers_every_profile)
{
    const int n = getNumSurpriseProfiles();
    GF_EXPECT (n >= 3);

    for (int seed : { 0, 1, 7, 99, 4242, 9999 })
    {
        int prev = evolve::profileForCycle (-60, seed, n);

        for (long long c = -59; c < 600; ++c)
        {
            const auto p = evolve::profileForCycle (c, seed, n);
            GF_EXPECT_MSG (p >= 0 && p < n, "profile range");
            GF_EXPECT_MSG (p != prev, "immediate repeat at cycle " + juce::String ((int) c) + " seed " + juce::String (seed));
            prev = p;
        }

        // Every block of n consecutive cycles is a permutation.
        for (long long block = -5; block < 20; ++block)
        {
            std::set<int> seen;
            for (int i = 0; i < n; ++i)
                seen.insert (evolve::profileForCycle (block * n + i, seed, n));

            GF_EXPECT_MSG ((int) seen.size() == n, "block " + juce::String ((int) block) + " is not a permutation");
        }
    }

    // Different seeds give different orders.
    int differing = 0;
    for (long long c = 0; c < 32; ++c)
        differing += evolve::profileForCycle (c, 1, n) != evolve::profileForCycle (c, 2, n) ? 1 : 0;
    GF_EXPECT (differing > 8);
}

GF_TEST (Evolve, random_access_seek_equals_play_through)
{
    const double ppqs[] { 0.0, 5.5, 31.9, 32.0, 40.0, 63.99, 64.0, 130.2, 511.0, 1000.7, 3.0, 40.0 };

    std::vector<EngineSettings> forward;

    for (auto ppq : ppqs)
    {
        auto s = evolving (8);
        evolve::apply (s, ppq, transport (ppq));
        forward.push_back (s);
    }

    // Visit the same positions backwards, interleaved with unrelated calls: no state may leak.
    for (int i = (int) std::size (ppqs) - 1; i >= 0; --i)
    {
        auto junk = evolving (16, 3);
        evolve::apply (junk, 999.0 + i, transport (999.0 + i));

        auto s = evolving (8);
        evolve::apply (s, ppqs[i], transport (ppqs[i]));
        GF_EXPECT (std::memcmp (&s, &forward[(size_t) i], sizeof (s)) == 0);
    }
}

GF_TEST (Evolve, blend_is_bounded_and_the_first_quarter_crossfades_the_waypoints)
{
    for (int period : { 8, 16, 32 })
    {
        const double bars = period * 4.0;     // beats per cycle in 4/4

        for (double ppq = 0.0; ppq < bars * 4.0; ppq += bars / 40.0)
        {
            auto s = evolving (period);
            evolve::Waypoint w;
            GF_EXPECT (evolve::waypointAt (s, ppq, transport (ppq), w));
            GF_EXPECT (w.blend >= 0.0f && w.blend <= 1.0f);

            const auto phase = ppq / bars - std::floor (ppq / bars);

            if (phase >= 0.25 + 1.0e-9)
                GF_EXPECT (w.blend == 1.0f);

            evolve::apply (s, ppq, transport (ppq));

            for (float v : { s.fill01, s.intensity01, s.complexity01, s.chaos01, s.density01 })
                GF_EXPECT (v >= 0.0f && v <= 1.0f);

            for (float wt : s.weights)
                GF_EXPECT (wt > 0.0f && wt <= 1.0f);                // a positive weight stays positive
        }
    }

    // At the start of a cycle the target is the PREVIOUS waypoint; after 25 % it is the current one.
    auto start = evolving (8);
    evolve::Waypoint w0;
    GF_EXPECT (evolve::waypointAt (start, 32.0, transport (32.0), w0));      // beat 32 = cycle 1, phase 0
    GF_EXPECT_NEAR (w0.blend, 0.0, 1.0e-6);
    GF_EXPECT (w0.previousProfile == evolve::profileForCycle (0, 7, getNumSurpriseProfiles()));
    GF_EXPECT (w0.profile == evolve::profileForCycle (1, 7, getNumSurpriseProfiles()));

    auto settled = evolving (8);
    evolve::apply (settled, 32.0 + 16.0, transport (48.0));                  // phase 0.5
    const auto& target = surpriseTargets (evolve::profileForCycle (1, 7, getNumSurpriseProfiles()));
    EngineSettings base = evolving (8);
    GF_EXPECT_NEAR (settled.fill01, base.fill01 + (target.fill - base.fill01) * evolve::kPull, 1.0e-5);
    GF_EXPECT_NEAR (settled.density01, base.density01 + (target.density - base.density01) * evolve::kPull, 1.0e-5);
}

GF_TEST (Evolve, ignores_everything_it_must_not_touch)
{
    auto s = evolving (16, 31);
    s.fillTypeIndex = 0;
    s.weights[(int) Algo::Stutter] = 0.0f;          // a user exclusion
    auto before = s;
    evolve::apply (s, 55.0, transport (55.0));

    GF_EXPECT (s.weights[(int) Algo::Stutter] == 0.0f);

    // Put the five creative values and the weights back: nothing else may differ.
    auto restored = s;
    restored.fill01 = before.fill01; restored.intensity01 = before.intensity01; restored.complexity01 = before.complexity01;
    restored.chaos01 = before.chaos01; restored.density01 = before.density01;
    for (int i = 0; i < numAlgos; ++i) restored.weights[i] = before.weights[i];
    GF_EXPECT (std::memcmp (&restored, &before, sizeof (before)) == 0);

    // A forced Fill Type keeps its weights untouched.
    auto forced = evolving (16);
    forced.fillTypeIndex = 4;
    const auto forcedBefore = forced;
    evolve::apply (forced, 55.0, transport (55.0));
    for (int i = 0; i < numAlgos; ++i)
        GF_EXPECT (forced.weights[i] == forcedBefore.weights[i]);
    GF_EXPECT (forced.fillTypeIndex == 4 && forced.seed == forcedBefore.seed);
}

GF_TEST (Evolve, invalid_transport_or_time_signature_is_neutral)
{
    for (auto t : { transport (20.0, 4, 4, false), transport (20.0, 0, 0), transport (20.0, 4, 3),
                    transport (std::nan (""), 4, 4) })
    {
        auto s = evolving (8);
        EngineSettings before;
        std::memcpy (&before, &s, sizeof (s));
        evolve::apply (s, t.ppq, t);
        GF_EXPECT (std::memcmp (&before, &s, sizeof (s)) == 0);
    }
}

GF_TEST (Evolve, waypoint_label_matches_the_profile)
{
    const int n = getNumSurpriseProfiles();
    GF_EXPECT (n == 8);

    for (int i = 0; i < n; ++i)
        GF_EXPECT (juce::String (surpriseTargets (i).name).isNotEmpty());

    GF_EXPECT (juce::String (surpriseTargets (7).name) == "Tape Breakdown");

    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 80.0f }, { "evolve", 1.0f }, { "seed", 5.0f } });
    TestPlayHead ph;
    ph.bpm = goldenBpm; ph.tsNum = 4; ph.tsDen = 4;
    std::set<int> seen;
    RenderOptions o;
    o.playHead = &ph;
    o.onBlock = [&] (int, int)
    {
        const auto p = proc.getFillEngine().getDecisionTelemetry().evolveProfile.load();

        if (p >= 0)
        {
            GF_EXPECT (p < n);
            seen.insert (p);
        }
    };

    renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, 24), 512, goldenSampleRate, o);
    GF_EXPECT_MSG (seen.size() >= 2, "Evolve never moved to a second waypoint");
}

GF_TEST (Evolve, block_size_invariant_decisions)
{
    std::vector<std::int64_t> reference;

    for (int block : { 32, 64, 512, 4096, 8192 })
    {
        const auto starts = startsFor (block, { { "fill", 70.0f }, { "evolve", 1.0f }, { "seed", 5.0f } });
        GF_EXPECT_MSG (! starts.empty(), "no fills at block " + juce::String (block));

        if (reference.empty())
            reference = starts;
        else
            GF_EXPECT_MSG (starts == reference, "starts differ at block " + juce::String (block));
    }
}

GF_TEST (Evolve, changes_the_decisions_when_on_and_a_transport_exists)
{
    const auto plain = startsFor (512, { { "fill", 40.0f } }, 16);
    const auto evolving16 = startsFor (512, { { "fill", 40.0f }, { "evolve", 1.0f }, { "seed", 3.0f } }, 16);
    GF_EXPECT (plain != evolving16);
}

GF_TEST (Evolve, surprise_is_unchanged_by_the_shared_table)
{
    const char* names[8] { "Subtle Sparkle", "Drum Edits", "Glitch Storm", "Dub Echoes", "Ambient Haze",
                           "Pitch Bender", "Industrial Grit", "Tape Breakdown" };

    for (int i = 0; i < 8; ++i)
        GF_EXPECT (juce::String (surpriseTargets (i).name) == names[i]);

    // surpriseTargets reads the same table as Surprise: Surprise's pick for profile i lands inside the
    // centred ranges and uses the same name.
    GapFillAudioProcessor proc;
    juce::Random rng (1234);
    int previous = -1;

    for (int round = 0; round < 40; ++round)
    {
        juce::String name;
        const auto index = surpriseParameters (proc.getValueTreeState(), rng, previous, name);
        GF_EXPECT (index != previous);
        GF_EXPECT (name == surpriseTargets (index).name);
        previous = index;
    }

    // Deterministic: same Random seed -> same picks and parameters.
    GapFillAudioProcessor a, b;
    juce::Random ra (99), rb (99);
    juce::String na, nb;
    GF_EXPECT (surpriseParameters (a.getValueTreeState(), ra, -1, na) == surpriseParameters (b.getValueTreeState(), rb, -1, nb));

    for (const auto& id : creativeParameterIDs())
        GF_EXPECT (a.getValueTreeState().getRawParameterValue (id)->load() == b.getValueTreeState().getRawParameterValue (id)->load());

    // Targets are clamped for out-of-range requests.
    GF_EXPECT (juce::String (surpriseTargets (-5).name) == names[0]);
    GF_EXPECT (juce::String (surpriseTargets (500).name) == names[7]);
}

GF_TEST (Evolve, state_round_trip_and_performance_id)
{
    GF_EXPECT (performanceParameterIDs().contains ("evolve"));
    GapFillAudioProcessor a;
    setParams (a, { { "evolve", 3.0f } });
    juce::MemoryBlock block;
    a.getStateInformation (block);
    GapFillAudioProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());
    GF_EXPECT (pi (b.getValueTreeState().getRawParameterValue ("evolve")) == 3);
}
