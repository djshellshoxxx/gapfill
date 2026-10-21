// F-L13 Sidechain-driven fills (docs/dev/12 F-L13).

#include "../support/TestRig.h"

#include "features/scdrive/ScDrive.h"

#include <cmath>
#include <cstring>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr double kBpm = 150.0;

    struct Run
    {
        juce::AudioBuffer<float> out;
        std::vector<std::int64_t> starts;
        std::vector<std::uint32_t> generations;
        int gated = 0;
        int fills = 0;
        int newest = -1;
    };

    using Params = std::initializer_list<std::pair<const char*, float>>;

    Run render (Params params, const juce::AudioBuffer<float>* side, int block = 512, int bars = 4)
    {
        GapFillAudioProcessor proc;
        setParams (proc, params);
        TestPlayHead ph;
        ph.bpm = kBpm;
        RenderOptions o;
        o.playHead = &ph;
        o.sidechain = side;
        auto r = renderOffline (proc, drumLoop (kSr, kBpm, bars), block, kSr, o);
        proc.drainFillEvents();

        Run run;
        run.out = std::move (r.out);
        run.fills = r.fills;
        run.gated = proc.getFillEngine().getRejectCount (RejectReason::ScGated);
        run.newest = (int) proc.getScTracker().newestKnownPos();

        for (int i = proc.getGapOutcomeLog().size() - 1; i >= 0; --i)
        {
            const auto& e = proc.getGapOutcomeLog().get (i);

            if (e.type == FillEventType::Started)
            {
                run.starts.push_back (e.pos);
                run.generations.push_back (e.generation);
            }
        }

        return run;
    }

    bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
            return false;

        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
                return false;

        return true;
    }

    juce::AudioBuffer<float> constantNoise (float amp, int bars = 4)
    {
        return makeNoise (kSr, 60.0 / kBpm * 4.0 * bars + 0.5, amp);
    }

    juce::AudioBuffer<float> silence (int bars = 4)
    {
        return makeSilence (2, (int) (kSr * (60.0 / kBpm * 4.0 * bars + 0.5)));
    }

    /** A 40 ms noise burst every `periodSeconds`, first one at `offsetSeconds`. */
    juce::AudioBuffer<float> pulses (double periodSeconds, double offsetSeconds, int bars = 4, float amp = 0.5f)
    {
        const auto total = (int) (kSr * (60.0 / kBpm * 4.0 * bars + 0.5));
        auto b = makeSilence (2, total);
        juce::Random rng (7);

        for (double t = offsetSeconds; t * kSr < total - 2000; t += periodSeconds)
            for (int i = 0; i < (int) (0.04 * kSr); ++i)
                for (int ch = 0; ch < 2; ++ch)
                    b.setSample (ch, (int) (t * kSr) + i, amp * (rng.nextFloat() * 2.0f - 1.0f));

        return b;
    }

    /** Noise that is on for `onSeconds` and off for `offSeconds`, starting on at t = 0. */
    juce::AudioBuffer<float> dutyCycle (double onSeconds, double offSeconds, int bars = 4, float amp = 0.5f)
    {
        const auto total = (int) (kSr * (60.0 / kBpm * 4.0 * bars + 0.5));
        auto b = makeSilence (2, total);
        juce::Random rng (13);

        for (double t = 0.0; t * kSr < total; t += onSeconds + offSeconds)
            for (int i = 0; i < (int) (onSeconds * kSr) && (int) (t * kSr) + i < total; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    b.setSample (ch, (int) (t * kSr) + i, amp * (rng.nextFloat() * 2.0f - 1.0f));

        return b;
    }

    /** A 8 ms noise click every 55 ms (quiet in between, so the tracker sees a clean onset each time). */
    juce::AudioBuffer<float> snapSource (int bars = 4)
    {
        const auto total = (int) (kSr * (60.0 / kBpm * 4.0 * bars + 0.5));
        auto b = makeSilence (2, total);
        juce::Random rng (17);

        for (double t = 0.1; t * kSr < total - 1000; t += 0.055)
            for (int i = 0; i < (int) (0.008 * kSr); ++i)
                for (int ch = 0; ch < 2; ++ch)
                    b.setSample (ch, (int) (t * kSr) + i, 0.5f * (rng.nextFloat() * 2.0f - 1.0f));

        return b;
    }

    const Params kBusy { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f } };
}

//==============================================================================
// Tracker (unit)

GF_TEST (ScDrive, tracker_levels_follow_the_signal_and_onsets_are_found)
{
    scdrive::SidechainOnsetTracker t;
    t.prepare (kSr);

    std::vector<float> sig ((size_t) kSr, 0.0f);          // 1 s: silence, a burst from 0.5 s
    juce::Random rng (3);

    for (int i = (int) (0.5 * kSr); i < (int) (0.6 * kSr); ++i)
        sig[(size_t) i] = 0.5f * (rng.nextFloat() * 2.0f - 1.0f);

    for (int pos = 0; pos < (int) sig.size(); pos += 480)
        t.process (sig.data() + pos, juce::jmin (480, (int) sig.size() - pos), pos, -30.0f);

    GF_EXPECT (t.covers (1000) && t.covers (24000 + 3000));
    GF_EXPECT (t.levelDbAt (4800) < -100.0f);                                            // silence
    GF_EXPECT (t.levelDbAt (24000 + 2400) > -20.0f && t.levelDbAt (24000 + 2400) < -5.0f);   // ~ -10 dBFS (0.5 uniform noise)
    GF_EXPECT (t.onsetCount() == 1);
    const auto onset = t.nearestOnsetWithin (23000, 3000);
    GF_EXPECT_MSG (onset >= 24000 - 64 && onset <= 24000 + 480, "onset at " + juce::String ((int) onset));
    GF_EXPECT (t.nearestOnsetWithin (25000, 3000) < 0);
}

GF_TEST (ScDrive, tracker_keeps_only_the_last_eight_onsets_and_restarts_on_a_gap_in_the_stream)
{
    scdrive::SidechainOnsetTracker t;
    t.prepare (kSr);
    juce::Random rng (5);
    std::vector<float> sig ((size_t) (kSr * 6.0), 0.0f);

    for (int k = 0; k < 12; ++k)
        for (int i = 0; i < 2400; ++i)
            sig[(size_t) ((int) (kSr * 0.2) + k * (int) (kSr * 0.4) + i)] = 0.4f * (rng.nextFloat() * 2.0f - 1.0f);

    for (int pos = 0; pos < (int) sig.size(); pos += 512)
        t.process (sig.data() + pos, juce::jmin (512, (int) sig.size() - pos), pos, -30.0f);

    GF_EXPECT (t.onsetCount() == scdrive::SidechainOnsetTracker::kOnsets);
    GF_EXPECT (t.nearestOnsetWithin (0, (int) (kSr * 1.0)) < 0);                         // the early ones are gone

    t.process (sig.data(), 512, 10 * (int) kSr, -30.0f);                                 // not contiguous: restart
    GF_EXPECT (t.onsetCount() == 0);
    GF_EXPECT (! t.covers (5 * (int) kSr));
}

GF_TEST (ScDrive, gate_is_inert_without_a_tracker_or_without_data_and_follows_the_level_otherwise)
{
    scdrive::ScSettings s;
    s.gate = scdrive::GateWhenQuiet;
    GF_EXPECT (scdrive::evaluateGate (s, 1000) == RejectReason::None);                   // no tracker

    scdrive::SidechainOnsetTracker t;
    t.prepare (kSr);
    s.tracker = &t;
    GF_EXPECT (scdrive::evaluateGate (s, 1000) == RejectReason::None);                   // no data yet

    std::vector<float> sig (4800, 0.0f);
    for (int i = 2400; i < 4800; ++i)
        sig[(size_t) i] = 0.5f;

    t.process (sig.data(), 4800, 0, -30.0f);
    s.threshDb = -30.0f;

    s.gate = scdrive::GateWhenQuiet;
    GF_EXPECT (scdrive::evaluateGate (s, 1000) == RejectReason::None);                   // quiet here
    GF_EXPECT (scdrive::evaluateGate (s, 4000) == RejectReason::ScGated);                // loud here
    s.gate = scdrive::GateWhenActive;
    GF_EXPECT (scdrive::evaluateGate (s, 1000) == RejectReason::ScGated);
    GF_EXPECT (scdrive::evaluateGate (s, 4000) == RejectReason::None);
    s.gate = scdrive::GateOff;
    GF_EXPECT (scdrive::evaluateGate (s, 4000) == RejectReason::None);
}

GF_TEST (ScDrive, snap_never_before_the_decision_and_respects_min_gap_and_lookahead)
{
    scdrive::SidechainOnsetTracker t;
    t.prepare (kSr);
    std::vector<float> sig ((size_t) kSr, 0.0f);
    juce::Random rng (9);

    for (int i = 0; i < 2000; ++i)                       // an onset at 0.50 s
        sig[(size_t) (24000 + i)] = 0.5f * (rng.nextFloat() * 2.0f - 1.0f);

    t.process (sig.data(), (int) sig.size(), 0, -30.0f);
    const auto onset = t.nearestOnsetWithin (0, 48000);
    GF_EXPECT (onset >= 0);

    scdrive::ScSettings s;
    s.tracker = &t;
    s.snap01 = 1.0f;                                      // 30 ms window
    const std::int64_t hop = 256, minGap = 960;
    auto room = [] (std::int64_t) -> std::int64_t { return 100000; };

    // decision 20 ms before the onset: snapped to the hop nearest the onset, strictly later than the decision
    const auto decision = onset - (std::int64_t) (0.020 * kSr);
    const auto target = scdrive::chooseSnapTarget (s, decision, hop, kSr, (std::int64_t) (0.1 * kSr), minGap, room);
    GF_EXPECT (target > decision);
    GF_EXPECT (std::abs (target - onset) <= hop / 2);
    GF_EXPECT (target % hop == 0);

    // the onset is before the decision: never earlier than the gap start
    GF_EXPECT (scdrive::chooseSnapTarget (s, onset + 1000, hop, kSr, (std::int64_t) (0.1 * kSr), minGap, room) < 0);

    // outside the snap window (decision 50 ms before the onset): no snap
    GF_EXPECT (scdrive::chooseSnapTarget (s, onset - (std::int64_t) (0.050 * kSr), hop, kSr, (std::int64_t) (0.1 * kSr), minGap, room) < 0);

    // a smaller snap amount shrinks the window
    s.snap01 = 0.25f;                                     // 7.5 ms
    GF_EXPECT (scdrive::chooseSnapTarget (s, decision, hop, kSr, (std::int64_t) (0.1 * kSr), minGap, room) < 0);
    s.snap01 = 1.0f;

    // not enough room left after the snap: evaluate now
    auto tight = [&] (std::int64_t at) { return at > decision ? (std::int64_t) 500 : (std::int64_t) 100000; };
    GF_EXPECT (scdrive::chooseSnapTarget (s, decision, hop, kSr, (std::int64_t) (0.1 * kSr), minGap, tight) < 0);

    // bounded by the lookahead: only 10 ms visible, the onset is 20 ms away
    GF_EXPECT (scdrive::chooseSnapTarget (s, decision, hop, kSr, (std::int64_t) (0.010 * kSr), minGap, room) < 0);

    // snap off
    s.snap01 = 0.0f;
    GF_EXPECT (scdrive::chooseSnapTarget (s, decision, hop, kSr, (std::int64_t) (0.1 * kSr), minGap, room) < 0);
}

//==============================================================================
// Whole plug-in

GF_TEST (ScDrive, parameters_are_registered_neutral_and_never_randomised)
{
    GapFillAudioProcessor proc;
    auto& v = proc.getValueTreeState();
    GF_EXPECT (v.getRawParameterValue ("scSnap")->load() == 0.0f);
    GF_EXPECT (v.getRawParameterValue ("scGate")->load() == 0.0f);
    GF_EXPECT (v.getRawParameterValue ("scThresh")->load() == -30.0f);

    for (auto* id : { "scSnap", "scGate", "scThresh" })
    {
        GF_EXPECT (performanceParameterIDs().contains (id));
        GF_EXPECT (! randomisableParameterIDs().contains (id));
    }
}

GF_TEST (ScDrive, off_identity_even_with_a_sidechain_connected_and_zero_cost)
{
    const auto side = constantNoise (0.5f);
    const auto base = render (kBusy, &side);
    const auto explicitOff = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f },
                                       { "scGate", 0.0f }, { "scSnap", 0.0f }, { "scThresh", -10.0f } }, &side);
    GF_EXPECT (identical (base.out, explicitOff.out));
    GF_EXPECT (base.fills > 0);
    GF_EXPECT (base.gated == 0);
    GF_EXPECT (base.newest < 0 && explicitOff.newest < 0);          // the tracker never ran
}

GF_TEST (ScDrive, inert_without_a_sidechain_bus)
{
    const auto base = render (kBusy, nullptr);
    const auto gated = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f },
                                 { "scGate", 2.0f }, { "scSnap", 100.0f } }, nullptr);
    GF_EXPECT (identical (base.out, gated.out));
    GF_EXPECT (gated.gated == 0);
    GF_EXPECT (gated.newest < 0);
}

GF_TEST (ScDrive, gate_quiet_blocks_when_the_sidechain_is_active_and_passes_when_it_is_silent)
{
    const auto loud = constantNoise (0.5f);
    const auto quiet = silence();
    const auto base = render (kBusy, &quiet);

    const auto blocked = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "scGate", 1.0f } }, &loud);
    GF_EXPECT (blocked.gated > 0);
    GF_EXPECT (blocked.fills == 0);
    GF_EXPECT (blocked.newest >= 0);                                 // the tracker ran

    const auto open = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "scGate", 1.0f } }, &quiet);
    GF_EXPECT (open.gated == 0);
    GF_EXPECT (identical (base.out, open.out));                       // an open gate changes nothing
}

GF_TEST (ScDrive, gate_active_requires_signal)
{
    const auto loud = constantNoise (0.5f);
    const auto quiet = silence();
    const auto base = render (kBusy, &loud);

    const auto open = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "scGate", 2.0f } }, &loud);
    GF_EXPECT (open.gated == 0);
    GF_EXPECT (identical (base.out, open.out));

    const auto blocked = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "scGate", 2.0f } }, &quiet);
    GF_EXPECT (blocked.gated > 0);
    GF_EXPECT (blocked.fills == 0);
}

GF_TEST (ScDrive, threshold_moves_the_decision_point)
{
    const auto side = constantNoise (0.01f);                       // about -45 dBFS RMS
    const auto below = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "scGate", 2.0f }, { "scThresh", -30.0f } }, &side);
    const auto above = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "scGate", 2.0f }, { "scThresh", -60.0f } }, &side);
    GF_EXPECT (below.gated > 0 && below.fills == 0);
    GF_EXPECT (above.gated == 0 && above.fills > 0);
}

GF_TEST (ScDrive, rhythmic_sidechain_gates_some_gaps_and_not_others)
{
    const auto side = dutyCycle (0.17, 0.13);                      // loud 170 ms, quiet 130 ms (a 300 ms period never locks to the beat grid)
    const auto base = render (kBusy, &side);

    for (float mode : { 1.0f, 2.0f })
    {
        const auto gated = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "scGate", mode } }, &side);
        GF_EXPECT_MSG (gated.gated > 0, "mode " + juce::String (mode));
        GF_EXPECT_MSG (gated.fills < base.fills, "mode " + juce::String (mode));
        GF_EXPECT_MSG (gated.fills > 0, "mode " + juce::String (mode));
    }
}

GF_TEST (ScDrive, snap_delays_fills_but_never_moves_them_earlier)
{
    const auto side = snapSource();                                // a short click every 55 ms: an onset is always near
    const auto base = render (kBusy, &side);
    const auto snapped = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "scSnap", 100.0f },
                                   { "scThresh", -40.0f } }, &side);

    GF_EXPECT (base.fills > 0 && snapped.fills > 0);
    GF_EXPECT (snapped.newest >= 0);

    int compared = 0, delayed = 0;

    for (size_t i = 0; i < snapped.starts.size(); ++i)
        for (size_t j = 0; j < base.starts.size(); ++j)
            if (base.generations[j] == snapped.generations[i])
            {
                ++compared;
                GF_EXPECT_MSG (snapped.starts[i] >= base.starts[j], "snapped " + juce::String ((int) snapped.starts[i])
                                                                   + " before gap fill " + juce::String ((int) base.starts[j]));
                GF_EXPECT (snapped.starts[i] - base.starts[j] <= (std::int64_t) (0.030 * kSr) + 256);
                delayed += snapped.starts[i] > base.starts[j] ? 1 : 0;
            }

    GF_EXPECT (compared > 0);
    GF_EXPECT (delayed > 0);
}

GF_TEST (ScDrive, decisions_are_block_size_invariant)
{
    const auto side = snapSource();
    const Params p { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "scSnap", 100.0f }, { "scThresh", -40.0f } };
    const auto a = render (p, &side, 128);
    const auto b = render (p, &side, 480);
    const auto c = render (p, &side, 2048);
    GF_EXPECT (a.starts.size() > 0);
    GF_EXPECT (a.starts == b.starts && a.starts == c.starts);
    GF_EXPECT (a.gated == b.gated && a.gated == c.gated);
}

GF_TEST (ScDrive, works_with_each_detection_source)
{
    const auto loud = constantNoise (0.5f);

    for (float src : { 0.0f, 1.0f, 2.0f })
    {
        const auto r = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f },
                                 { "detSource", src }, { "scGate", 1.0f }, { "scSnap", 50.0f } }, &loud);
        GF_EXPECT_MSG (r.gated > 0, "detSource " + juce::String (src));

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < r.out.getNumSamples(); ++i)
                GF_EXPECT (std::isfinite (r.out.getSample (ch, i)));
    }
}

GF_TEST (ScDrive, the_reason_is_reachable_and_has_text)
{
    GF_EXPECT (std::string (rejectReasonName (RejectReason::ScGated)) == "ScGated");
    const auto loud = constantNoise (0.5f);
    const auto r = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "scGate", 1.0f } }, &loud);
    GF_EXPECT (r.gated > 0);
}
