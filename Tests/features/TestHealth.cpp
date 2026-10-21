// N-USE-b Transport & Sidechain Health (docs/dev/12 N-USE-b).

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include "features/health/Health.h"
#include "features/reasons/Reasons.h"

using namespace gapfill;
using namespace gapfill::health;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kChunk = 480;                 // 10 ms

    /** Runs the monitor for `seconds` with fixed observations and returns the mask. */
    std::uint32_t run (HealthMonitor& m, Observations o, double seconds)
    {
        o.numSamples = kChunk;
        o.sampleRate = kSr;

        for (int i = 0; i < (int) std::lround (seconds * 100.0); ++i)
            m.update (o);

        return m.getMask();
    }

    Observations healthy()
    {
        Observations o;
        o.transportValid = true;
        o.scPeak = 0.5f;
        return o;
    }

    bool has (std::uint32_t mask, HealthCode c)   { return (mask & bitOf (c)) != 0; }
}

GF_TEST (Health, healthy_shows_nothing)
{
    HealthMonitor m;
    GF_EXPECT (run (m, healthy(), 30.0) == 0);

    HealthCode c;
    GF_EXPECT (! highestVisible (0, 0, c));
}

GF_TEST (Health, each_code_set_and_cleared_with_hysteresis)
{
    struct Case { HealthCode code; void (*provoke) (Observations&); double setAfter; };

    const Case cases[] =
    {
        { HealthCode::NoTransport,        [] (Observations& o) { o.tempoFeatureOn = true; o.transportValid = false; }, 1.0 },
        { HealthCode::NoSidechain,        [] (Observations& o) { o.scRequested = true; o.scBusEnabled = false; }, 0.5 },
        { HealthCode::SidechainSilent,    [] (Observations& o) { o.scRequested = true; o.scBusEnabled = true; o.scPeak = 0.0f; }, 2.0 },
        { HealthCode::FreeTimingFallback, [] (Observations& o) { o.freeTimingFallback = true; }, 0.5 },
        { HealthCode::OverloadWarn,       [] (Observations& o) { o.cpu01 = 0.8f; }, 2.0 },
        { HealthCode::OverloadCritical,   [] (Observations& o) { o.cpu01 = 0.95f; }, 0.25 },
        { HealthCode::BypassActive,       [] (Observations& o) { o.bypassed = true; }, 0.0 },
        { HealthCode::PanicActive,        [] (Observations& o) { o.panicActive = true; }, 0.0 },
    };

    for (const auto& c : cases)
    {
        HealthMonitor m;
        auto o = healthy();
        c.provoke (o);

        // Not set before the hold time ...
        if (c.setAfter > 0.1)
            GF_EXPECT_MSG (! has (run (m, o, c.setAfter - 0.12), c.code), "set too early");

        // ... set once it has held, ...
        GF_EXPECT_MSG (has (run (m, o, 0.5 + 0.12), c.code) || has (run (m, o, 0.5), c.code), "never set");
        GF_EXPECT (has (run (m, o, 3.0), c.code));

        // ... and cleared only after the clear hold (2 s; panic at once).
        const auto timing = HealthMonitor::timingFor (c.code);

        if (timing.clear > 0.5f)
            GF_EXPECT_MSG (has (run (m, healthy(), timing.clear - 0.3), c.code), "cleared too early");

        GF_EXPECT_MSG (! has (run (m, healthy(), 2.0), c.code), "never cleared");
    }
}

GF_TEST (Health, flicker_inside_the_hold_time_never_shows)
{
    HealthMonitor m;
    auto bad = healthy();
    bad.scRequested = true;
    bad.scBusEnabled = false;               // NoSidechain: 0.5 s hold

    for (int i = 0; i < 20; ++i)            // 0.3 s on, 0.1 s off, repeatedly
    {
        run (m, bad, 0.3);
        run (m, healthy(), 0.1);
        GF_EXPECT (! has (m.getMask(), HealthCode::NoSidechain));
    }

    // Once shown, a short gap in the condition does not clear it.
    run (m, bad, 1.0);
    GF_EXPECT (has (m.getMask(), HealthCode::NoSidechain));
    run (m, healthy(), 0.5);
    GF_EXPECT (has (m.getMask(), HealthCode::NoSidechain));
    run (m, bad, 0.1);
    run (m, healthy(), 1.5);
    GF_EXPECT (has (m.getMask(), HealthCode::NoSidechain));
}

GF_TEST (Health, pulses_latch_for_ten_seconds)
{
    HealthMonitor m;
    auto o = healthy();
    o.transportJumped = true;
    o.droppedEvents = 3;
    o.numSamples = kChunk;
    o.sampleRate = kSr;
    m.update (o);

    GF_EXPECT (has (m.getMask(), HealthCode::TransportJump) && has (m.getMask(), HealthCode::QueueOverflow));
    GF_EXPECT (has (run (m, healthy(), 9.5), HealthCode::TransportJump));
    GF_EXPECT (! has (run (m, healthy(), 1.5), HealthCode::TransportJump));
    GF_EXPECT (! has (m.getMask(), HealthCode::QueueOverflow));

    // A deadline overrun is critical for two seconds even when the average CPU is low.
    auto ov = healthy();
    ov.deadlineOverrun = true;
    ov.numSamples = kChunk;
    ov.sampleRate = kSr;
    m.update (ov);
    run (m, healthy(), 0.5);
    GF_EXPECT (has (m.getMask(), HealthCode::OverloadCritical));
}

GF_TEST (Health, sidechain_absent_vs_silent)
{
    auto absent = healthy();
    absent.scRequested = true;
    absent.scBusEnabled = false;

    auto silent = healthy();
    silent.scRequested = true;
    silent.scBusEnabled = true;
    silent.scPeak = 1.0e-5f;                // -100 dBFS

    auto playing = healthy();
    playing.scRequested = true;
    playing.scBusEnabled = true;
    playing.scPeak = 0.1f;

    HealthMonitor a, s, p, notRequested;
    const auto ma = run (a, absent, 3.0);
    const auto ms = run (s, silent, 3.0);

    GF_EXPECT (has (ma, HealthCode::NoSidechain) && ! has (ma, HealthCode::SidechainSilent));
    GF_EXPECT (has (ms, HealthCode::SidechainSilent) && ! has (ms, HealthCode::NoSidechain));
    GF_EXPECT (run (p, playing, 5.0) == 0);

    auto idle = healthy();
    idle.scBusEnabled = false;              // detSource == Main: an unused sidechain is not a problem
    GF_EXPECT (run (notRequested, idle, 5.0) == 0);

    // A burst of signal resets the silence counter.
    HealthMonitor burst;
    run (burst, silent, 1.5);
    run (burst, playing, 0.05);
    GF_EXPECT (! has (run (burst, silent, 1.5), HealthCode::SidechainSilent));
    GF_EXPECT (has (run (burst, silent, 1.0), HealthCode::SidechainSilent));
}

GF_TEST (Health, no_transport_only_when_feature_needs_it)
{
    auto noTransport = healthy();
    noTransport.transportValid = false;

    HealthMonitor off, on;
    GF_EXPECT (! has (run (off, noTransport, 5.0), HealthCode::NoTransport));

    noTransport.tempoFeatureOn = true;
    GF_EXPECT (has (run (on, noTransport, 5.0), HealthCode::NoTransport));
}

GF_TEST (Health, priority_order)
{
    const auto& order = priorityOrder();
    GF_EXPECT ((int) order.size() == numHealthCodes);
    GF_EXPECT (order[0] == HealthCode::PanicActive && order[1] == HealthCode::OverloadCritical);

    HealthCode c;
    const auto all = allCodesMask;
    GF_EXPECT (highestVisible (all, 0, c) && c == HealthCode::PanicActive);

    auto mask = all & ~bitOf (HealthCode::PanicActive);
    GF_EXPECT (highestVisible (mask, 0, c) && c == HealthCode::OverloadCritical);
    mask &= ~bitOf (HealthCode::OverloadCritical);
    GF_EXPECT (highestVisible (mask, 0, c) && c == HealthCode::NoSidechain);
    mask &= ~bitOf (HealthCode::NoSidechain);
    GF_EXPECT (highestVisible (mask, 0, c) && c == HealthCode::SidechainSilent);
    mask &= ~bitOf (HealthCode::SidechainSilent);
    GF_EXPECT (highestVisible (mask, 0, c) && c == HealthCode::NoTransport);
    mask &= ~bitOf (HealthCode::NoTransport);
    GF_EXPECT (highestVisible (mask, 0, c) && c == HealthCode::FreeTimingFallback);
    mask &= ~bitOf (HealthCode::FreeTimingFallback);
    GF_EXPECT (highestVisible (mask, 0, c) && c == HealthCode::OverloadWarn);
    mask &= ~bitOf (HealthCode::OverloadWarn);
    GF_EXPECT (highestVisible (mask, 0, c) && c == HealthCode::QueueOverflow);
    mask &= ~bitOf (HealthCode::QueueOverflow);
    GF_EXPECT (highestVisible (mask, 0, c) && c == HealthCode::BypassActive);
    mask &= ~bitOf (HealthCode::BypassActive);
    GF_EXPECT (highestVisible (mask, 0, c) && c == HealthCode::TransportJump);
    mask &= ~bitOf (HealthCode::TransportJump);
    GF_EXPECT (! highestVisible (mask, 0, c));
}

GF_TEST (Health, message_text_matches_code)
{
    DescribeContext ctx;
    ctx.cpu01 = 0.82f;
    ctx.fallbackBpm = 120.0;

    GF_EXPECT (describe (HealthCode::NoSidechain, ctx).message == "No sidechain - using input");
    GF_EXPECT (describe (HealthCode::NoTransport, ctx).message == "Transport unavailable - free timing at 120 BPM");
    GF_EXPECT (describe (HealthCode::OverloadWarn, ctx).message == "CPU high (82%) - try Quality: Balanced");
    GF_EXPECT (describe (HealthCode::SidechainSilent, ctx).message.contains ("silent"));
    GF_EXPECT (describe (HealthCode::PanicActive, ctx).message.contains ("Recover"));
    GF_EXPECT (describe (HealthCode::BypassActive, ctx).message.contains ("Bypass"));

    for (int i = 0; i < numHealthCodes; ++i)
    {
        const auto d = describe ((HealthCode) i, ctx);
        GF_EXPECT (d.message.isNotEmpty() && d.explanation.isNotEmpty() && d.action.isNotEmpty());
    }

    GF_EXPECT (describe (HealthCode::Count, ctx).message.isEmpty());
}

GF_TEST (Health, muted_code_hidden_but_listed)
{
    HealthTracker tracker;
    const auto mask = bitOf (HealthCode::NoSidechain) | bitOf (HealthCode::BypassActive);
    tracker.update (mask, 5000);
    tracker.update (mask | bitOf (HealthCode::OverloadWarn), 7000);

    const auto muted = bitOf (HealthCode::NoSidechain);
    HealthCode shown;
    GF_EXPECT (highestVisible (mask, muted, shown) && shown == HealthCode::BypassActive);

    HealthTracker::Entry entries[numHealthCodes];
    const auto n = tracker.list (entries, numHealthCodes, muted);
    GF_EXPECT (n == 3);
    GF_EXPECT (entries[0].code == HealthCode::NoSidechain && entries[0].muted && entries[0].onsetMs == 5000);
    GF_EXPECT (entries[1].code == HealthCode::OverloadWarn && ! entries[1].muted && entries[1].onsetMs == 7000);
    GF_EXPECT_NEAR (tracker.secondsActive (HealthCode::OverloadWarn, 9500), 2.5, 1.0e-4);
    GF_EXPECT (tracker.secondsActive (HealthCode::PanicActive, 9500) == 0.0f);

    // Safety messages cannot be muted.
    GF_EXPECT (! isMutable (HealthCode::PanicActive) && ! isMutable (HealthCode::OverloadCritical));
    GF_EXPECT (highestVisible (bitOf (HealthCode::PanicActive), bitOf (HealthCode::PanicActive), shown));
}

GF_TEST (Health, footer_selection_follows_the_u9_order_for_this_package)
{
    using F = FooterKind;
    GF_EXPECT (selectFooter ({}) == F::Nothing);
    GF_EXPECT (selectFooter ({ true, true, true }) == F::Health);
    GF_EXPECT (selectFooter ({ false, true, true }) == F::Fixit);
    GF_EXPECT (selectFooter ({ false, false, true }) == F::LastGap);
}

GF_TEST (Health, footer_rate_limited)
{
    reasons::StatusRateLimiter limiter (250);
    int updates = 0;

    for (std::uint32_t t = 0; t < 2000; t += 10)       // 100 Hz of attempted updates
        updates += limiter.allow (t) ? 1 : 0;

    GF_EXPECT (updates == 8);                          // 4 Hz over 2 s
}

GF_TEST (Health, mask_publish_no_alloc)
{
    HealthMonitor m;
    auto o = healthy();
    o.numSamples = kChunk;
    o.sampleRate = kSr;
    m.update (o);                                      // warm up

    AllocGuard guard;

    for (int i = 0; i < 1000; ++i)
    {
        o.cpu01 = (i % 200) < 100 ? 0.95f : 0.1f;
        o.droppedEvents = (i % 400) == 0 ? 1u : 0u;
        o.scRequested = (i % 300) < 150;
        m.update (o);
    }

    GF_EXPECT (guard.count() == 0);
}

GF_TEST (Health, processor_publishes_codes_from_real_blocks)
{
    // Sidechain requested but the bus is off: NoSidechain appears after its hold time.
    GapFillAudioProcessor proc;
    setParams (proc, { { "detSource", 1.0f } });
    renderGoldenScenarioA (proc);
    GF_EXPECT (has (proc.getExplainState().healthMonitor.getMask(), HealthCode::NoSidechain));

    // The default (Main detection) with a transport raises nothing.
    GapFillAudioProcessor healthyProc;
    setParams (healthyProc, { { "syncEnable", 0.0f } });
    renderGoldenScenarioA (healthyProc);
    GF_EXPECT (healthyProc.getExplainState().healthMonitor.getMask() == 0);

    // Bypass shows up too.
    GapFillAudioProcessor bypassed;
    setParams (bypassed, { { "bypass", 1.0f }, { "syncEnable", 0.0f } });
    renderGoldenScenarioA (bypassed);
    GF_EXPECT (has (bypassed.getExplainState().healthMonitor.getMask(), HealthCode::BypassActive));
}

GF_TEST (Health, muted_codes_round_trip_through_the_state_subtree)
{
    GapFillAudioProcessor a;
    a.getExplainState().mutedHealth.store (bitOf (HealthCode::NoSidechain) | bitOf (HealthCode::PanicActive));
    a.getExplainState().showSuggestions.store (false);
    a.getExplainState().legendCollapsed.store (true);
    a.getExplainState().timelineZoomSeconds.store (16);

    juce::MemoryBlock saved;
    a.getStateInformation (saved);

    GapFillAudioProcessor b;
    b.setStateInformation (saved.getData(), (int) saved.getSize());
    GF_EXPECT ((b.getExplainState().mutedHealth.load() & bitOf (HealthCode::NoSidechain)) != 0);
    GF_EXPECT ((b.getExplainState().mutedHealth.load() & bitOf (HealthCode::PanicActive)) == 0);   // never mutable
    GF_EXPECT (! b.getExplainState().showSuggestions.load());
    GF_EXPECT (b.getExplainState().legendCollapsed.load());
    GF_EXPECT (b.getExplainState().timelineZoomSeconds.load() == 16);

    // Defaults write nothing; a malformed subtree is dropped alone.
    GapFillAudioProcessor defaults;
    juce::MemoryBlock plain;
    defaults.getStateInformation (plain);
    auto xml = juce::AudioProcessor::getXmlFromBinary (plain.getData(), (int) plain.getSize());
    GF_EXPECT (xml != nullptr && xml->getChildByName ("Health") == nullptr);

    auto bad = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    bad->getChildByName ("Health")->setAttribute ("zoom", 7);
    bad->getChildByName ("Health")->setAttribute ("muted", 99999);
    juce::MemoryBlock badBlock;
    juce::AudioProcessor::copyXmlToBinary (*bad, badBlock);

    GapFillAudioProcessor c;
    c.getExplainState().showSuggestions.store (false);
    c.setStateInformation (badBlock.getData(), (int) badBlock.getSize());
    GF_EXPECT (c.getExplainState().showSuggestions.load());           // reset to the default
    GF_EXPECT (c.getExplainState().mutedHealth.load() == 0);
}
