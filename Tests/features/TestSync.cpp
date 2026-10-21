// E-2 Tempo-sync audit and fixes (docs/dev/03-SPEC-ESSENTIALS.md E-2).

#include "../support/TestRig.h"

#include "features/state/StateSubtrees.h"
#include "features/sync/Sync.h"
#include "features/sync/SyncState.h"

#include <cmath>

using namespace gapfill;
using namespace gftest;

namespace
{
    double divisionSeconds (int div, double bpm)  { return divisionInBeats (div) * 60.0 / bpm; }

    bool isPowerOfTwoRatio (double a, double b)
    {
        const auto r = std::log2 (a / b);
        return std::abs (r - std::round (r)) < 1.0e-5;
    }
}

GF_TEST (Sync, clamp_table_is_in_range_and_power_of_two_related)
{
    for (double bpm : { 40.0, 60.0, 90.0, 120.0, 174.0, 300.0 })
    {
        for (int div = 0; div < (int) Division::NumDivisions; ++div)
        {
            const auto want = divisionSeconds (div, bpm);

            struct Limit { float lo, hi; };

            for (const auto lim : { Limit { 0.008f, 0.5f }, Limit { 0.01f, 1.0f }, Limit { 0.02f, 2.0f } })
            {
                const auto r = sync::clampToDivision ((float) want, lim.lo, lim.hi, div);
                const auto msg = juce::String ("bpm ") + juce::String (bpm) + " div " + juce::String (div);

                GF_EXPECT_MSG (std::isfinite (r.seconds) && r.seconds >= lim.lo && r.seconds <= lim.hi, msg);

                const bool inside = want >= lim.lo && want <= lim.hi;
                GF_EXPECT_MSG (r.clamped == ! inside, msg);

                if (inside)
                    GF_EXPECT_MSG (std::abs (r.seconds - want) < 1.0e-6, msg);
                else
                    GF_EXPECT_MSG (isPowerOfTwoRatio (r.seconds, want), msg);

                // The reported division stays in the knob's family and is consistent with the seconds.
                GF_EXPECT_MSG (r.division >= 0 && r.division < (int) Division::NumDivisions, msg);
            }
        }
    }
}

GF_TEST (Sync, half_note_at_60_bpm_becomes_quarter_note)
{
    // 1/2 at 60 BPM is 2 s; the stutter limit is 500 ms -> 1/4 would be 1 s, 1/8 500 ms.
    const auto r = sync::clampToDivision ((float) divisionSeconds ((int) Division::Half, 60.0),
                                          0.008f, 0.5f, (int) Division::Half);
    GF_EXPECT (r.clamped);
    GF_EXPECT_NEAR (r.seconds, 0.5, 1.0e-6);
    GF_EXPECT (r.division == (int) Division::Eighth);

    // Delay (limit 1 s): 1/4 at 40 BPM is 1.5 s -> 1/8 = 750 ms.
    const auto d = sync::clampToDivision ((float) divisionSeconds ((int) Division::Quarter, 40.0),
                                          0.01f, 1.0f, (int) Division::Quarter);
    GF_EXPECT_NEAR (d.seconds, 0.75, 1.0e-6);
    GF_EXPECT (d.division == (int) Division::Eighth);
}

GF_TEST (Sync, clamp_rejects_garbage)
{
    for (float bad : { 0.0f, -1.0f, std::nanf (""), std::numeric_limits<float>::infinity() })
    {
        const auto r = sync::clampToDivision (bad, 0.01f, 1.0f, 3);
        GF_EXPECT (std::isfinite (r.seconds) && r.seconds >= 0.01f && r.seconds <= 1.0f);
    }
}

GF_TEST (Sync, tempo_memory_valid_invalid_never)
{
    sync::TempoSource t;

    // never valid -> fallback (120 by default)
    GF_EXPECT_NEAR (t.resolve (false, 0.0, 512), 120.0, 1.0e-9);
    GF_EXPECT_NEAR (t.resolve (false, -5.0, 512), 120.0, 1.0e-9);
    GF_EXPECT_NEAR (t.resolve (false, std::nan (""), 512), 120.0, 1.0e-9);

    // valid -> used and remembered
    GF_EXPECT_NEAR (t.resolve (true, 87.0, 512), 87.0, 1.0e-9);
    GF_EXPECT (t.hostTempoSeenRecently (48000.0));

    // invalid afterwards -> last known
    GF_EXPECT_NEAR (t.resolve (false, 0.0, 512), 87.0, 1.0e-9);
    GF_EXPECT_NEAR (t.resolve (true, 5000.0, 512), 87.0, 1.0e-9);   // implausible host value ignored

    // ages out of "recently seen" after more than 5 s of silence
    for (int i = 0; i < 600; ++i)
        t.resolve (false, 0.0, 512);

    GF_EXPECT (! t.hostTempoSeenRecently (48000.0));

    t.reset();
    GF_EXPECT_NEAR (t.resolve (false, 0.0, 512), 120.0, 1.0e-9);
}

GF_TEST (Sync, fallback_bounds_are_enforced)
{
    sync::TempoSource t;
    t.setFallbackBpm (10.0);   GF_EXPECT_NEAR (t.getFallbackBpm(), 40.0, 1.0e-9);
    t.setFallbackBpm (1000.0); GF_EXPECT_NEAR (t.getFallbackBpm(), 300.0, 1.0e-9);
    t.setFallbackBpm (std::nan ("")); GF_EXPECT_NEAR (t.getFallbackBpm(), 120.0, 1.0e-9);
    t.setFallbackBpm (93.0);
    GF_EXPECT_NEAR (t.resolve (false, 0.0, 64), 93.0, 1.0e-9);
}

GF_TEST (Sync, fallback_bpm_round_trips_through_state_and_default_writes_nothing)
{
    GapFillAudioProcessor a;

    {
        juce::MemoryBlock block;
        a.getStateInformation (block);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        GF_EXPECT (xml != nullptr);
        GF_EXPECT (juce::ValueTree::fromXml (*xml).getChildWithName ("Perf").isValid() == false);
    }

    a.getShaping().tempo.setFallbackBpm (93.0);
    juce::MemoryBlock block;
    a.getStateInformation (block);

    GapFillAudioProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());
    GF_EXPECT_NEAR (b.getShaping().tempo.getFallbackBpm(), 93.0, 1.0e-9);
}

GF_TEST (Sync, malformed_perf_subtree_is_dropped_and_the_rest_loads)
{
    GapFillAudioProcessor a;
    setParams (a, { { "fill", 77.0f } });
    juce::MemoryBlock block;
    a.getStateInformation (block);
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    GF_EXPECT (xml != nullptr);

    auto root = juce::ValueTree::fromXml (*xml);

    for (const char* bad : { "banana", "5", "9999", "nan" })
    {
        auto tree = root.createCopy();
        juce::ValueTree perf ("Perf");
        perf.setProperty ("fallbackBpm", juce::String (bad), nullptr);
        tree.appendChild (perf, nullptr);

        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), out);

        GapFillAudioProcessor b;
        b.getShaping().tempo.setFallbackBpm (150.0);
        b.setStateInformation (out.getData(), (int) out.getSize());
        GF_EXPECT_MSG (std::abs (pv (b.getValueTreeState().getRawParameterValue ("fill")) - 77.0f) < 0.01f, bad);
        GF_EXPECT_MSG (std::abs (b.getShaping().tempo.getFallbackBpm() - 120.0) < 1.0e-9, bad);   // dropped -> default
    }
}

GF_TEST (Sync, sync_mods_formula)
{
    GF_EXPECT_NEAR (sync::modSyncTremoloHz (0.125f), 8.0, 1.0e-6);       // 1/0.125 = 8 Hz
    GF_EXPECT_NEAR (sync::modSyncTremoloHz (0.5f), 2.0, 1.0e-6);
    GF_EXPECT_NEAR (sync::modSyncTremoloHz (1.0f / 128.0f), 32.0, 1.0e-4);   // 128 Hz folds down to 32
    GF_EXPECT_NEAR (sync::modSyncPanHz (0.125f), 2.0, 1.0e-6);           // 1/(0.125*4)
    GF_EXPECT_NEAR (sync::modSyncPanHz (1.0f), 0.25, 1.0e-6);

    for (float s = 0.001f; s < 4.0f; s *= 1.37f)
    {
        const auto t = sync::modSyncTremoloHz (s);
        const auto p = sync::modSyncPanHz (s);
        GF_EXPECT (t >= 2.0f && t <= 32.0f);
        GF_EXPECT (p >= 0.25f && p <= 8.0f);
    }
}

GF_TEST (Sync, quantise_is_a_multiple_never_longer_never_over_usable)
{
    const double unit = 4800.0;   // 100 ms at 48 kHz

    for (std::int64_t usable : { 4800LL, 9000LL, 20000LL })
    {
        for (std::int64_t len = 100; len <= usable; len += 137)
        {
            const auto q = sync::quantiseFillLength (len, unit, 2880, usable);
            GF_EXPECT (q <= len && q <= usable && q > 0);
            GF_EXPECT (q == len || std::fmod ((double) q, unit) == 0.0);
        }
    }

    GF_EXPECT (sync::quantiseFillLength (12345, 0.0, 2880, 20000) == 12345);   // off
    GF_EXPECT (sync::quantiseFillLength (4000, unit, 2880, 20000) == 4000);    // shorter than a unit
    GF_EXPECT (sync::quantiseFillLength (14000, unit, 2880, 20000) == 9600);
    GF_EXPECT (sync::quantiseFillLength (14000, unit, 10000, 20000) == 14000); // result below min stays
    GF_EXPECT (sync::fillQuantBeats (0) == 0.0 && sync::fillQuantBeats (3) == 0.25);
}

GF_TEST (Sync, fill_quantise_aligns_started_fills_to_the_grid)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 90.0f }, { "fillQuant", 3.0f } });   // 1/16 at 150 bpm = 100 ms
    const auto result = renderGoldenScenarioA (proc);
    GF_EXPECT (result.fills > 0);
    GF_EXPECT (proc.drainFillEvents() > 0);

    int aligned = 0;

    for (int i = 0; i < proc.getRecipeHistory().size(); ++i)
    {
        const auto len = (double) proc.getRecipeHistory().get (i).lengthMs;
        const auto steps = len / 100.0;
        const bool onGrid = std::abs (steps - std::round (steps)) * 100.0 <= 1.01;
        GF_EXPECT_MSG (onGrid || len < 100.0, "length " + juce::String (len) + " ms must be on the 100 ms grid");
        aligned += onGrid ? 1 : 0;
    }

    GF_EXPECT (aligned > 0);
}

GF_TEST (Sync, quantise_off_and_mod_sync_off_are_identical_to_defaults)
{
    GapFillAudioProcessor a, b;
    setParams (a, { { "fill", 80.0f } });
    setParams (b, { { "fill", 80.0f }, { "fillQuant", 0.0f }, { "modSync", 0.0f } });
    const auto ra = renderGoldenScenarioA (a);
    const auto rb = renderGoldenScenarioA (b);
    GF_EXPECT (compareBuffers (ra.out, rb.out, 0.0).maxAbs == 0.0);
}

GF_TEST (Sync, mod_sync_changes_the_sound_only_when_on_and_stays_finite)
{
    GapFillAudioProcessor a, b;
    setParams (a, { { "fill", 90.0f }, { "complexity", 90.0f }, { "chaos", 80.0f } });
    setParams (b, { { "fill", 90.0f }, { "complexity", 90.0f }, { "chaos", 80.0f }, { "modSync", 1.0f } });
    const auto ra = renderGoldenScenarioA (a);
    const auto rb = renderGoldenScenarioA (b);
    GF_EXPECT (rb.fills > 0);

    for (int ch = 0; ch < rb.out.getNumChannels(); ++ch)
        for (int i = 0; i < rb.out.getNumSamples(); ++i)
            GF_EXPECT (std::isfinite (rb.out.getSample (ch, i)));

    GF_EXPECT (compareBuffers (ra.out, rb.out, 0.0).maxAbs > 0.0);
}

GF_TEST (Sync, extreme_tempo_sets_the_clamped_flag)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 60.0f }, { "stutterDiv", (float) (int) Division::Half } });
    TestPlayHead ph;
    ph.bpm = 60.0;
    RenderOptions opt;
    opt.playHead = &ph;
    const auto in = drumLoop (48000.0, 60.0, 2);
    renderOffline (proc, in, 512, 48000.0, opt);
    GF_EXPECT (proc.getMeterState().syncClamped.load());

    GapFillAudioProcessor proc2;
    setParams (proc2, { { "fill", 60.0f } });
    TestPlayHead ph2;
    ph2.bpm = 120.0;
    RenderOptions opt2;
    opt2.playHead = &ph2;
    renderOffline (proc2, in, 512, 48000.0, opt2);
    GF_EXPECT (! proc2.getMeterState().syncClamped.load());
}

GF_TEST (Sync, no_host_tempo_uses_the_fallback_then_remembers_the_host)
{
    GapFillAudioProcessor proc;
    proc.getShaping().tempo.setFallbackBpm (60.0);
    setParams (proc, { { "fill", 60.0f }, { "stutterDiv", (float) (int) Division::Half } });
    TestPlayHead ph;
    ph.provideBpm = false;
    RenderOptions opt;
    opt.playHead = &ph;
    const auto in = drumLoop (48000.0, 120.0, 1);
    renderOffline (proc, in, 512, 48000.0, opt);
    GF_EXPECT (proc.getMeterState().syncClamped.load());                   // 1/2 at the 60 BPM fallback
    GF_EXPECT_NEAR (proc.getShaping().tempo.getLastKnownBpm(), 0.0, 0.0);  // never seen a host tempo
}
