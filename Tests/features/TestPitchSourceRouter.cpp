#if defined (GF_CONTEXT_STANDALONE)
 #include "ContextStandalone.h"
 #include <iostream>
#else
 #include "../TestFramework.h"
 #include "../support/AllocGuard.h"
#endif
#include "../../Source/features/harmonic/psola/PitchSourceRouter.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
using namespace gapfill::harmonic::psola;
namespace
{
    std::vector<float> sourceTone (double rate, double hz, double seconds)
    {
        std::vector<float> result ((std::size_t) (rate * seconds));
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            const double phase = 6.283185307179586 * hz * (double) i / rate;
            result[i] = (float) (0.2 * std::sin (phase) + 0.1 * std::sin (phase * 2));
        }
        return result;
    }
    void route (PitchSourceRouter& router, const std::vector<float>& input, int block,
                PitchSourceChunk chunk)
    {
        for (std::size_t offset = 0; offset < input.size();)
        {
            const int n = (int) std::min<std::size_t> ((std::size_t) block, input.size() - offset);
            router.push (input.data() + offset, n, chunk);
            chunk.liveFirst += n;
            if (chunk.historyWritten) chunk.historyFirst += n;
            offset += (std::size_t) n;
        }
    }
    void expectPitch (const PitchSourceRouter::Query& result, double hz)
    {
        GF_EXPECT (result.voiced && std::abs (1200 * std::log2 (result.f0Hz / hz)) <= 20);
    }
}
GF_TEST (PitchSourceRouter, incoming_held_audio_never_changes_the_original_history_pitch)
{
    PitchSourceRouter router; router.prepare (48000); router.setActive (true);
    const auto old = sourceTone (48000, 220, 1);
    route (router, old, 512, {});
    expectPitch (router.query (24000, 47999), 220);
    PitchSourceChunk held; held.liveFirst = held.historyFirst = 48000;
    held.held = true; held.historyWritten = false;
    route (router, sourceTone (48000, 440, 3), 64, held);
    expectPitch (router.query (24000, 47999), 220);
    GF_EXPECT (! router.query (48000, 60000).voiced);
}
GF_TEST (PitchSourceRouter, snapshot_frames_are_remapped_and_frozen_when_copy_starts)
{
    PitchSourceRouter router; router.prepare (48000); router.setActive (true);
    route (router, sourceTone (48000, 220, 1), 512, {});
    PitchSourceChunk held; held.liveFirst = held.historyFirst = 48000;
    held.held = true; held.historyWritten = false;
    route (router, sourceTone (48000, 440, 1), 512, held);
    held.liveFirst = 96000;
    held.copy = { 1, 0, 48000, 48000 };
    route (router, sourceTone (48000, 880, 1), 512, held); // long copy stays on old latch
    expectPitch (router.query (24000, 47999), 220);
    held.liveFirst = 144000; held.activeSnapshotIndex = 0; held.activeSnapshotGeneration = 1;
    route (router, sourceTone (48000, 110, 0.1), 64, held);
    expectPitch (router.query (12000, 47999), 440);
    GF_EXPECT (! router.query (48000, 49000).voiced);
}
GF_TEST (PitchSourceRouter, generation_prevents_snapshot_slot_ABA_and_missing_banks_are_unknown)
{
    PitchSourceRouter router; router.prepare (48000); router.setActive (true);
    PitchSourceChunk held; held.held = true; held.historyWritten = false;
    route (router, sourceTone (48000, 220, 1), 512, held);
    held.liveFirst = 48000; held.copy = { 1, 0, 0, 48000 };
    held.activeSnapshotIndex = 0; held.activeSnapshotGeneration = 1;
    route (router, sourceTone (48000, 440, 1), 512, held);
    expectPitch (router.query (12000, 47000), 220);
    held.liveFirst = 96000; held.copy = { 2, 1, 48000, 48000 };
    held.activeSnapshotIndex = 1; held.activeSnapshotGeneration = 2;
    route (router, sourceTone (48000, 880, 1), 512, held);
    expectPitch (router.query (12000, 47000), 440);
    held.liveFirst = 144000; held.copy = { 3, 0, 96000, 48000 };
    held.activeSnapshotIndex = 0; held.activeSnapshotGeneration = 1; // stale observer
    route (router, sourceTone (48000, 110, 0.1), 64, held);
    GF_EXPECT (! router.query (12000, 47000).voiced);
    held.liveFirst = 148800; held.activeSnapshotGeneration = 3;
    route (router, sourceTone (48000, 110, 0.1), 64, held);
    expectPitch (router.query (12000, 47000), 880);
    held.liveFirst = 153600; held.activeSnapshotGeneration = 4;
    route (router, sourceTone (48000, 110, 0.1), 64, held);
    GF_EXPECT (! router.query (12000, 47000).voiced);
}
GF_TEST (PitchSourceRouter, a_release_join_rejects_windows_crossing_the_paused_history_segment)
{
    PitchSourceRouter router; router.prepare (48000); router.setActive (true);
    route (router, sourceTone (48000, 220, 1), 512, {});
    PitchSourceChunk held; held.liveFirst = held.historyFirst = 48000;
    held.held = true; held.historyWritten = false;
    route (router, sourceTone (48000, 440, 1), 512, held);
    PitchSourceChunk released; released.liveFirst = 96000; released.historyFirst = 48000;
    route (router, sourceTone (48000, 880, 0.03), 64, released);
    GF_EXPECT (! router.query (48000, 49439).voiced); // no complete new analysis footprint
    released.liveFirst += 1440; released.historyFirst += 1440;
    route (router, sourceTone (48000, 880, 0.5), 64, released);
    expectPitch (router.query (60000, 73439), 880);
    GF_EXPECT (! router.query (47000, 73439).voiced);
}
GF_TEST (PitchSourceRouter, held_only_tap_coordinate_restart_preserves_old_history_bank)
{
    PitchSourceRouter router; router.prepare (48000); router.setActive (true);
    PitchSourceChunk normal; normal.liveFirst = normal.historyFirst = 200000;
    route (router, sourceTone (48000, 220, 1), 512, normal);
    PitchSourceChunk held; held.historyFirst = 248000; held.held = true; held.historyWritten = false;
    route (router, sourceTone (48000, 440, 1), 512, held); // new detailed tap starts at0
    expectPitch (router.query (224000, 247999), 220);
    held.liveFirst = 48000; held.copy = { 9, 1, 0, 48000 };
    held.activeSnapshotIndex = 1; held.activeSnapshotGeneration = 9;
    route (router, sourceTone (48000, 880, 0.1), 512, held);
    expectPitch (router.query (12000, 47000), 440);
    PitchSourceChunk released; released.liveFirst = released.historyFirst = 248000;
    route (router, sourceTone (48000, 110, 0.5), 512, released);
    expectPitch (router.query (260000, 271999), 110);
    GF_EXPECT (! router.query (240000, 271999).voiced);
}
GF_TEST (PitchSourceRouter, cold_enable_while_held_is_unknown_until_a_new_source_copy)
{
    PitchSourceRouter router; router.prepare (48000);
    PitchSourceChunk held; held.liveFirst = 900000; held.historyFirst = 48000;
    held.held = true; held.historyWritten = false;
    const auto input = sourceTone (48000, 440, 1);
    route (router, input, 512, held);
    router.setActive (true);
    route (router, input, 512, held);
    GF_EXPECT (! router.query (0, 47999).voiced);
    held.liveFirst += 48000; held.copy = { 11, 0, 900000, 48000 };
    held.activeSnapshotIndex = 0; held.activeSnapshotGeneration = 11;
    route (router, input, 512, held);
    expectPitch (router.query (12000, 47000), 440);
}
GF_TEST (PitchSourceRouter, full_two_second_snapshots_remain_queryable_at_four_rates)
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        PitchSourceRouter router; router.prepare (rate); router.setActive (true);
        PitchSourceChunk held; held.held = true; held.historyWritten = false;
        route (router, sourceTone (rate, 220, 3), 512, held);
        held.liveFirst = (std::int64_t) (3 * rate);
        held.copy = { 1, 0, (std::int64_t) rate, (int) (2 * rate) };
        held.activeSnapshotIndex = 0; held.activeSnapshotGeneration = 1;
        route (router, sourceTone (rate, 440, 0.1), 64, held);
        // Both early and late scan windows must use the old source tone.
        expectPitch (router.query ((std::int64_t) (0.15 * rate), (std::int64_t) (0.35 * rate)), 220);
        expectPitch (router.query ((std::int64_t) (1.7 * rate), (std::int64_t) (1.9 * rate)), 220);
        GF_EXPECT (sizeof (router) <= 40 * 1024 && router.detector().maxMacsPerHop() <= 8192);
    }
}
GF_TEST (PitchSourceRouter, chunk_partition_and_invalid_descriptor_do_not_leak_stale_pitch)
{
    const auto input = sourceTone (48000, 220, 1);
    PitchSourceRouter baseline; baseline.prepare (48000); baseline.setActive (true);
    route (baseline, input, 32, {});
    const auto expected = baseline.query (12000, 47000);
    for (int block : { 64, 512, 4096 })
    {
        PitchSourceRouter router; router.prepare (48000); router.setActive (true);
        route (router, input, block, {});
        const auto got = router.query (12000, 47000);
        GF_EXPECT (got.voiced && got.f0Hz == expected.f0Hz && got.clarity == expected.clarity);
        PitchSourceChunk invalid; invalid.liveFirst = std::numeric_limits<std::int64_t>::max() - 1;
        router.push (input.data(), 32, invalid);
        GF_EXPECT (! router.query (12000, 47000).voiced);
        router.setActive (false);
        GF_EXPECT (! router.isActive() && ! router.query (12000, 47000).voiced);
    }
}
#if ! defined (GF_CONTEXT_STANDALONE)
GF_TEST (PitchSourceRouter, freeze_copy_query_and_release_do_not_allocate)
{
    PitchSourceRouter router; router.prepare (48000); router.setActive (true);
    const auto input = sourceTone (48000, 220, 1);
    long allocations = 0;
    PitchSourceRouter::Query result;
    {
        gftest::AllocGuard guard;
        route (router, input, 64, {});
        PitchSourceChunk held; held.liveFirst = held.historyFirst = 48000;
        held.held = true; held.historyWritten = false;
        held.copy = { 1, 0, 0, 48000 }; held.activeSnapshotIndex = 0; held.activeSnapshotGeneration = 1;
        route (router, input, 64, held);
        result = router.query (12000, 47000);
        PitchSourceChunk released; released.liveFirst = 96000; released.historyFirst = 48000;
        route (router, input, 64, released);
        router.reset();
        allocations = guard.count();
    }
    GF_EXPECT (allocations == 0 && result.voiced);
}
#endif
#if defined (GF_CONTEXT_STANDALONE)
int main()
{
    int failures = 0;
    for (const auto& [name, fn] : contexttest::cases())
        try { fn(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    return failures != 0 ? 1 : 0;
}
#endif
