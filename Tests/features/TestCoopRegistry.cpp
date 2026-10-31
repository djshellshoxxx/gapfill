// Pure bounded registry checks; the election test file supplies the standalone
// registry/main when both files are compiled with GF_COOP_STANDALONE.
#include <array>
#include <atomic>
#include <thread>

#if defined (GF_COOP_STANDALONE)
 #include "CoopStandalone.h"
#else
 #include "../TestFramework.h"
#endif

#include "../../Source/features/coop/DetRegistry.h"

using namespace gapfill::coop::deterministic;

GF_TEST (CoopRegistry, coherent_initial_roster_is_independent_of_first_render_order)
{
    Registry registry;
    std::array<Handle, 4> members;
    for (int i = 0; i < 4; ++i)
        members[(std::size_t) i] = registry.claim (1, (std::uint8_t) (i + 1), 50, Grid::Bar);
    GF_EXPECT (registry.maintain (1, 4.0));
    std::uint64_t generation = 0;
    for (int i : { 3, 1, 2, 0 })
    {
        View view;
        GF_EXPECT (registry.admit (members[(std::size_t) i], 0.0, 1.0, view));
        GF_EXPECT (view.current.snapshot.count == 4);
        if (generation == 0)
            generation = view.current.snapshot.generation;
        GF_EXPECT (view.current.snapshot.generation == generation);
        GF_EXPECT (decisionAt (view, 0.5, true, 100.0f).ready);
    }
}

GF_TEST (CoopRegistry, full_registry_refuses_a_33rd_claim_and_never_reuses_lifetime_tokens)
{
    Registry registry;
    std::array<Handle, 32> handles;
    for (int i = 0; i < 32; ++i)
    {
        handles[(std::size_t) i] = registry.claim (1, (std::uint8_t) (i + 1), 50, Grid::Bar);
        GF_EXPECT (handles[(std::size_t) i].token != 0);
    }
    GF_EXPECT (registry.claim (2, 1, 50, Grid::Bar).token == 0);
    auto stale = handles[0];
    registry.release (handles[0]);
    auto replacement = registry.claim (1, 1, 50, Grid::Bar);
    GF_EXPECT (replacement.token != 0 && replacement.token != stale.token);
    registry.release (stale);
    GF_EXPECT (registry.owns (replacement));             // stale handle cannot release a new occupant
    for (auto& h : handles)
        registry.release (h);
    registry.release (replacement);
    GF_EXPECT (registry.liveCount() == 0);
}

GF_TEST (CoopRegistry, updates_activate_at_one_shared_future_boundary)
{
    Registry registry;
    auto a = registry.claim (1, 1, 50, Grid::Bar);
    auto b = registry.claim (1, 2, 50, Grid::Bar);
    GF_EXPECT (registry.maintain (1, 4.0));
    View av, bv;
    GF_EXPECT (registry.admit (a, 0.0, 1.0, av));
    GF_EXPECT (registry.admit (b, 0.0, 1.0, bv));
    const auto original = av.current.snapshot.generation;
    GF_EXPECT (registry.update (b, 1, 2, 100, Grid::Bar));
    GF_EXPECT (registry.maintain (1, 4.0));
    GF_EXPECT (registry.admit (a, 1.0, 2.0, av));
    GF_EXPECT (registry.admit (b, 1.0, 2.0, bv));
    GF_EXPECT (av.current.activationTick == bv.current.activationTick);
    const double boundary = (double) av.current.activationTick / kTicksPerBeat;
    GF_EXPECT (boundary > 2.0);
    GF_EXPECT (decisionAt (av, 1.5, true, 100.0f).generation == original);
    GF_EXPECT (decisionAt (bv, 1.5, true, 100.0f).generation == original);
    GF_EXPECT (decisionAt (av, boundary, true, 100.0f).generation == original + 1);
    GF_EXPECT (decisionAt (bv, boundary, true, 100.0f).generation == original + 1);
    // Never overwrite a version while a live member still needs its predecessor.
    GF_EXPECT (registry.update (b, 1, 2, 20, Grid::Bar));
    GF_EXPECT (! registry.maintain (1, 4.0));
    GF_EXPECT (registry.admit (a, boundary, boundary + 1.0, av));
    GF_EXPECT (! registry.maintain (1, 4.0));
    GF_EXPECT (registry.admit (b, boundary, boundary + 1.0, bv));
    GF_EXPECT (registry.maintain (1, 4.0));
}

GF_TEST (CoopRegistry, duplicate_slots_are_reported_until_repaired_and_groups_are_independent)
{
    Registry registry;
    auto a = registry.claim (1, 1, 50, Grid::Bar);
    auto b = registry.claim (1, 1, 50, Grid::Bar);
    auto other = registry.claim (2, 1, 50, Grid::Bar);
    GF_EXPECT (! registry.maintain (1, 4.0));
    GF_EXPECT (registry.maintain (2, 4.0));
    GF_EXPECT (registry.lowestFreeSlot (1, b.token) == 2);
    GF_EXPECT (registry.update (b, 1, 2, 50, Grid::Bar));
    GF_EXPECT (registry.maintain (1, 4.0));
    View view;
    GF_EXPECT (registry.admit (other, 0.0, 1.0, view));
    GF_EXPECT (view.current.snapshot.count == 1);
    GF_EXPECT (decisionAt (view, 0.5, true, 100.0f).owner);
    registry.release (a);
    registry.release (b);
    registry.release (other);
}

GF_TEST (CoopRegistry, claims_and_releases_from_16_threads_leave_no_live_record)
{
    Registry registry;
    std::atomic<int> invalid { 0 };
    std::array<std::thread, 16> threads;
    for (int i = 0; i < 16; ++i)
        threads[(std::size_t) i] = std::thread ([&, i]
        {
            for (int round = 0; round < 1000; ++round)
            {
                auto h = registry.claim ((std::uint8_t) (i % 8 + 1), (std::uint8_t) (i + 1), 50, Grid::Bar);
                if (h.token != 0)
                {
                    if (! registry.owns (h))
                        invalid.fetch_add (1);
                    registry.release (h);
                }
            }
        });
    for (auto& thread : threads)
        thread.join();
    GF_EXPECT (invalid.load() == 0 && registry.liveCount() == 0);
}

GF_TEST (CoopRegistry, automatic_duplicate_repair_keeps_the_first_claim_and_reserves_unique_slots)
{
    Registry registry;
    std::array<Handle, 4> members;
    for (auto& member : members)
        member = registry.claim (1, 1, 50, Grid::Bar);
    GF_EXPECT (registry.repairSlot (1, members[0].token) == 0);
    GF_EXPECT (registry.repairSlot (1, members[1].token) == 2);
    GF_EXPECT (registry.repairSlot (1, members[2].token) == 3);
    GF_EXPECT (registry.repairSlot (1, members[3].token) == 4);
    GF_EXPECT (registry.maintain (1, 4.0));
}

GF_TEST (CoopRegistry, concurrent_publication_never_places_activation_inside_an_admitted_callback)
{
    Registry registry;
    auto a = registry.claim (1, 1, 50, Grid::Bar);
    auto b = registry.claim (1, 2, 50, Grid::Bar);
    auto c = registry.claim (1, 3, 50, Grid::Bar);
    GF_EXPECT (registry.maintain (1, 4.0));
    struct Range { std::uint64_t generation = 0; double end = 0.0; };
    constexpr int count = 2000;
    std::array<Range, count> rangesA {}, rangesB {};
    std::array<double, count + 2> boundaries {};
    std::atomic<int> invalid { 0 };
    const auto render = [&] (const Handle& handle, auto& ranges)
    {
        View view;
        for (int i = 0; i < count; ++i)
        {
            const double ppq = i * 0.125;
            if (registry.admit (handle, ppq, ppq + 0.125, view))
            {
                ranges[(std::size_t) i] = { view.current.snapshot.generation, ppq + 0.125 };
                if (view.current.snapshot.count != 3)
                    invalid.fetch_add (1);
            }
        }
    };
    std::thread first ([&] { render (a, rangesA); });
    std::thread second ([&] { render (b, rangesB); });
    std::thread publisher ([&]
    {
        View view;
        for (int i = 0; i < count; ++i)
        {
            registry.update (c, 1, 3, (std::uint8_t) (i % 2 ? 25 : 75), Grid::Bar);
            registry.admit (c, i * 0.125, (i + 1) * 0.125, view);
            if (registry.maintain (1, 4.0))
            {
                Plan plan;
                if (registry.latest (1, plan) && plan.snapshot.generation < boundaries.size())
                    boundaries[(std::size_t) plan.snapshot.generation] = (double) plan.activationTick / kTicksPerBeat;
            }
        }
    });
    first.join(); second.join(); publisher.join();
    GF_EXPECT (invalid.load() == 0);
    for (const auto* ranges : { &rangesA, &rangesB })
        for (const auto& range : *ranges)
            if (range.generation != 0 && range.generation + 1 < boundaries.size()
                && boundaries[(std::size_t) range.generation + 1] != 0.0)
                GF_EXPECT (boundaries[(std::size_t) range.generation + 1] > range.end);
}

GF_TEST (CoopRegistry, cached_elections_change_at_each_musical_slot_and_reapply_current_yield)
{
    Registry registry;
    auto a = registry.claim (1, 1, 50, Grid::Bar);
    registry.claim (1, 2, 50, Grid::Bar);
    GF_EXPECT (registry.maintain (1, 4.0));
    View view;
    GF_EXPECT (registry.admit (a, 0.0, 1.0, view));
    for (int bar = 0; bar < 100; ++bar)
    {
        const auto expected = ownerSlot (view.current.snapshot, bar);
        const auto half = decisionAt (view, bar * 4.0, true, 50.0f);
        const auto full = decisionAt (view, bar * 4.0 + 0.5, true, 100.0f);
        GF_EXPECT (half.ownerSlot == expected && full.ownerSlot == expected);
        GF_EXPECT (half.scale.density == (expected == 1 ? 1.0f : 0.5f));
        GF_EXPECT (full.scale.density == (expected == 1 ? 1.0f : 0.0f));
    }
}
