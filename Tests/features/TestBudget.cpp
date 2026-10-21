// F-D01 Per-bar fill budget and quiet zones (docs/dev/12-SPEC-BACKLOG-AND-NEW.md F-D01).

#include "../support/TestRig.h"

#include "features/budget/Budget.h"

#include <cmath>
#include <cstring>
#include <map>

using namespace gapfill;
using namespace gftest;

namespace
{
    Effective::TransportInfo transport (double ppq = 0.0, int num = 4, int den = 4, bool valid = true)
    {
        return { valid, ppq, num, den, 120.0 };
    }

    budget::RecentFills fillsAt (std::initializer_list<double> ppqs)
    {
        budget::RecentFills r;
        for (auto p : ppqs)
            r.push (budget::tickFor (p));
        return r;
    }

    RejectReason verdict (int perBar, int zone, double ppq, const budget::RecentFills& r,
                          const PhraseSettings& ph = {}, int num = 4, int den = 4)
    {
        BudgetSettings b; b.perBar = perBar; b.zone = zone;
        return budget::evaluate (b, ph, ppq, transport (ppq, num, den), r);
    }

    struct Run
    {
        std::vector<std::int64_t> starts;
        std::vector<int> lengthsMs;
        int budgetFull = 0, quietZone = 0;
        double latency = 0.0;
    };

    Run render (int block, std::initializer_list<std::pair<const char*, float>> params, bool transportOn = true)
    {
        GapFillAudioProcessor proc;
        setParams (proc, params);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        ph.tsNum = transportOn ? 4 : 0; ph.tsDen = transportOn ? 4 : 0;
        ph.provideInfo = transportOn;
        RenderOptions o;
        o.playHead = &ph;
        renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, 8), block, goldenSampleRate, o);
        proc.drainFillEvents();

        Run r;
        r.latency = proc.getActiveLatencySamples();
        r.budgetFull = proc.getFillEngine().getRejectCount (RejectReason::BudgetFull);
        r.quietZone = proc.getFillEngine().getRejectCount (RejectReason::QuietZone);

        for (int i = proc.getGapOutcomeLog().size() - 1; i >= 0; --i)
        {
            const auto& e = proc.getGapOutcomeLog().get (i);

            if (e.type == FillEventType::Started)
            {
                r.starts.push_back (e.pos);
                r.lengthsMs.push_back (e.recipe.lengthMs);
            }
        }

        return r;
    }

    double ppqOf (std::int64_t pos, double latency)
    {
        return ((double) pos + latency) / (60.0 / goldenBpm * goldenSampleRate);
    }
}

GF_TEST (Budget, off_is_identity)
{
    BudgetSettings off;
    const auto r = fillsAt ({ 0.0, 0.5, 1.0, 1.5, 2.0 });
    GF_EXPECT (budget::evaluate (off, {}, 1.0, transport (1.0), r) == RejectReason::None);

    EngineSettings s;
    EngineSettings before;
    std::memcpy (&before, &s, sizeof (s));
    Effective::applyAtDecision (s, 3.0, transport (3.0));
    GF_EXPECT (std::memcmp (&before, &s, sizeof (s)) == 0);

    // Whole plugin: explicit Off equals the default render, with a transport that has a time signature.
    const auto a = render (512, { { "fill", 80.0f } });
    const auto b = render (512, { { "fill", 80.0f }, { "fillBudget", 0.0f }, { "quietZone", 0.0f } });
    GF_EXPECT (! a.starts.empty());
    GF_EXPECT (a.starts == b.starts && a.lengthsMs == b.lengthsMs);
    GF_EXPECT (b.budgetFull == 0 && b.quietZone == 0);
}

GF_TEST (Budget, budget_n_blocks_the_n_plus_first_fill)
{
    for (int n : { 1, 2, 3, 4, 6, 8 })
    {
        budget::RecentFills r;

        for (int k = 0; k < n; ++k)
        {
            GF_EXPECT_MSG (verdict (n, 0, 8.0 + 0.4 * k, r) == RejectReason::None, "fill " + juce::String (k + 1) + " of " + juce::String (n));
            r.push (budget::tickFor (8.0 + 0.4 * k));          // bar 2 of 4/4 (beats 8-12)
        }

        GF_EXPECT_MSG (verdict (n, 0, 8.0 + 0.4 * n, r) == RejectReason::BudgetFull, "budget " + juce::String (n));
    }
}

GF_TEST (Budget, a_new_bar_resets_the_count)
{
    const auto r = fillsAt ({ 8.0, 9.0, 10.0 });             // three fills in bar 2
    GF_EXPECT (verdict (3, 0, 11.0, r) == RejectReason::BudgetFull);
    GF_EXPECT (verdict (3, 0, 12.0, r) == RejectReason::None);   // bar 3 starts at beat 12
    GF_EXPECT (verdict (3, 0, 11.999, r) == RejectReason::BudgetFull);
}

GF_TEST (Budget, seek_back_recounts)
{
    FillEngine engine;
    engine.prepare (48000.0, 512, 2);
    GF_EXPECT (engine.getRecentFills().size() == 0);

    auto r = fillsAt ({ 8.0, 9.0 });
    GF_EXPECT (r.countInBar (2, 4.0) == 2);
    GF_EXPECT (verdict (2, 0, 10.0, r) == RejectReason::BudgetFull);

    // After a seek the ring is cleared and the bar is counted afresh.
    r.clear();
    GF_EXPECT (r.countInBar (2, 4.0) == 0);
    GF_EXPECT (verdict (2, 0, 10.0, r) == RejectReason::None);

    // Entries from other bars never count (a seek back to bar 1 sees only bar 1's fills).
    const auto mixed = fillsAt ({ 1.0, 5.0, 9.0, 13.0 });
    GF_EXPECT (mixed.countInBar (0, 4.0) == 1 && mixed.countInBar (1, 4.0) == 1 && mixed.countInBar (3, 4.0) == 1);
    GF_EXPECT (verdict (1, 0, 5.5, mixed) == RejectReason::BudgetFull);
    GF_EXPECT (verdict (1, 0, 2.5, mixed) == RejectReason::BudgetFull);   // bar 0 has one fill
    GF_EXPECT (verdict (2, 0, 2.5, mixed) == RejectReason::None);

    engine.noteTransportJump();
    GF_EXPECT (engine.getRecentFills().size() == 0);
}

GF_TEST (Budget, ring_keeps_the_last_sixteen)
{
    budget::RecentFills r;

    for (int i = 0; i < 40; ++i)
        r.push (budget::tickFor (0.1 * i));

    GF_EXPECT (r.size() == 16);
    GF_EXPECT (r.countInBar (0, 4.0) + r.countInBar (1, 4.0) == 16);   // only the latest 16 (ppq 2.4 .. 3.9) survive
}

GF_TEST (Budget, quiet_zone_geometry)
{
    const budget::RecentFills none;
    GF_EXPECT (verdict (0, budget::Beat1, 8.0, none) == RejectReason::QuietZone);
    GF_EXPECT (verdict (0, budget::Beat1, 8.99, none) == RejectReason::QuietZone);
    GF_EXPECT (verdict (0, budget::Beat1, 9.0, none) == RejectReason::None);
    GF_EXPECT (verdict (0, budget::LastBeat, 10.99, none) == RejectReason::None);
    GF_EXPECT (verdict (0, budget::LastBeat, 11.0, none) == RejectReason::QuietZone);
    GF_EXPECT (verdict (0, budget::LastTwoBeats, 9.99, none) == RejectReason::None);
    GF_EXPECT (verdict (0, budget::LastTwoBeats, 10.0, none) == RejectReason::QuietZone);

    // 6/8: 3 quarter beats per bar, last beat = positions 2..3.
    GF_EXPECT (verdict (0, budget::LastBeat, 5.5, none, {}, 6, 8) == RejectReason::QuietZone);   // 5.5 -> bar 1, pos 2.5
    GF_EXPECT (verdict (0, budget::LastBeat, 4.5, none, {}, 6, 8) == RejectReason::None);

    // Last bar of phrase needs a phrase.
    PhraseSettings ph; ph.bars = 4;
    GF_EXPECT (verdict (0, budget::LastBarOfPhrase, 13.0, none, ph) == RejectReason::QuietZone);
    GF_EXPECT (verdict (0, budget::LastBarOfPhrase, 11.9, none, ph) == RejectReason::None);
    GF_EXPECT (verdict (0, budget::LastBarOfPhrase, 13.0, none, PhraseSettings {}) == RejectReason::None);   // phrase Off

    // Zone applies even with Bias 0 (the zone is about the position only).
    ph.bias01 = 0.0f;
    GF_EXPECT (verdict (0, budget::LastBarOfPhrase, 13.0, none, ph) == RejectReason::QuietZone);

    // Offset moves the final bar.
    ph.offsetBars = 1;
    GF_EXPECT (verdict (0, budget::LastBarOfPhrase, 13.0, none, ph) == RejectReason::None);
    GF_EXPECT (verdict (0, budget::LastBarOfPhrase, 17.0, none, ph) == RejectReason::QuietZone);
}

GF_TEST (Budget, no_transport_or_time_signature_is_neutral)
{
    const auto r = fillsAt ({ 8.0, 9.0, 10.0 });
    BudgetSettings b; b.perBar = 1; b.zone = budget::Beat1;
    bool needs = false;

    GF_EXPECT (budget::evaluate (b, {}, 8.0, transport (8.0, 4, 4, false), r, &needs) == RejectReason::None && needs);
    GF_EXPECT (budget::evaluate (b, {}, 8.0, transport (8.0, 0, 0), r, &needs) == RejectReason::None && needs);
    GF_EXPECT (budget::evaluate (b, {}, 8.0, transport (8.0, 4, 3), r, &needs) == RejectReason::None && needs);
    GF_EXPECT (budget::evaluate (b, {}, std::nan (""), transport (std::nan ("")), r, &needs) == RejectReason::None && needs);

    // Whole plugin without a host time signature behaves exactly like Off.
    const auto off = render (512, { { "fill", 80.0f } }, false);
    const auto on = render (512, { { "fill", 80.0f }, { "fillBudget", 1.0f }, { "quietZone", 1.0f } }, false);
    GF_EXPECT (off.starts == on.starts && ! off.starts.empty());
    GF_EXPECT (on.budgetFull == 0 && on.quietZone == 0);
}

GF_TEST (Budget, enforced_in_the_plugin_and_reasons_are_reachable)
{
    const auto base = render (512, { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f } });
    const auto capped = render (512, { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "fillBudget", 1.0f } });
    GF_EXPECT (base.starts.size() > capped.starts.size());
    GF_EXPECT (capped.budgetFull > 0);

    std::map<long long, int> perBar;
    const double bar = 4.0;

    for (auto pos : capped.starts)
        ++perBar[(long long) std::floor (ppqOf (pos, capped.latency) / bar)];

    for (const auto& [index, count] : perBar)
        GF_EXPECT_MSG (count <= 1, "bar " + juce::String ((int) index) + " has " + juce::String (count) + " fills");

    // Quiet zone Beat 1: no fill starts in the first beat of a bar.
    const auto zoned = render (512, { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "quietZone", 1.0f } });
    GF_EXPECT (zoned.quietZone > 0);
    GF_EXPECT (! zoned.starts.empty());

    for (auto pos : zoned.starts)
    {
        const auto p = ppqOf (pos, zoned.latency);
        GF_EXPECT_MSG (budget::positionInBar (p, 4.0) >= 1.0 - 2.0e-3, "fill starts at beat position " + juce::String (budget::positionInBar (p, 4.0)));
    }
}

GF_TEST (Budget, a_running_fill_continues_through_the_zone)
{
    // Zone = last beat: fills start earlier in the bar and are allowed to run into it.
    const auto zoned = render (512, { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "maxFill", 800.0f },
                                       { "quietZone", 2.0f } });
    GF_EXPECT (! zoned.starts.empty());

    const double spb = 60.0 / goldenBpm * goldenSampleRate;
    int running = 0;

    for (size_t i = 0; i < zoned.starts.size(); ++i)
    {
        const auto startPpq = ppqOf (zoned.starts[i], zoned.latency);
        const auto endPpq = startPpq + zoned.lengthsMs[i] * 0.001 * goldenSampleRate / spb;
        GF_EXPECT (budget::positionInBar (startPpq, 4.0) < 3.0 + 2.0e-3);

        if (budget::positionInBar (endPpq, 4.0) >= 3.0 && budget::positionInBar (startPpq, 4.0) < 3.0
             && std::floor (endPpq / 4.0) == std::floor (startPpq / 4.0))
            ++running;
    }

    GF_EXPECT_MSG (running > 0, "no fill ran into the quiet zone");
}

GF_TEST (Budget, block_size_invariant)
{
    std::vector<std::int64_t> reference;

    for (int block : { 32, 64, 512, 4096, 8192 })
    {
        const auto r = render (block, { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f },
                                         { "fillBudget", 2.0f }, { "quietZone", 3.0f } });
        GF_EXPECT_MSG (! r.starts.empty(), "no fills at block " + juce::String (block));

        if (reference.empty())
            reference = r.starts;
        else
            GF_EXPECT_MSG (r.starts == reference, "starts differ at block " + juce::String (block));
    }
}

GF_TEST (Budget, telemetry_reports_the_bar_count_and_missing_transport)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "fillBudget", 2.0f } });
    TestPlayHead ph;
    ph.bpm = goldenBpm; ph.tsNum = 0; ph.tsDen = 0;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, 2), 512, goldenSampleRate, o);
    GF_EXPECT (proc.getFillEngine().getDecisionTelemetry().budgetNeedsTransport.load());
}
