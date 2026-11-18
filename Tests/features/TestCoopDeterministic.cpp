// F-X06 deterministic election. Also builds without JUCE:
// g++ -std=c++20 -pthread -Wall -Wextra -Werror -DGF_COOP_STANDALONE
// Tests/features/TestCoopDeterministic.cpp -o work/coop-model

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <thread>

#if defined (GF_COOP_STANDALONE)
 #include <iostream>
 #include "CoopStandalone.h"
#else
 #include "../TestFramework.h"
#endif

#include "../../Source/features/coop/Deterministic.h"

using namespace gapfill::coop::deterministic;

namespace
{
    Snapshot four()
    {
        Snapshot s;
        s.group = 1;
        s.generation = 9;
        s.count = 4;
        for (int i = 0; i < 4; ++i)
            s.members[(std::size_t) i] = { (std::uint64_t) (40 - i), (std::uint8_t) (i + 1), 50 };
        return s;
    }
}

GF_TEST (CoopDeterministic, four_members_have_exactly_one_owner_in_each_of_64_bars)
{
    const auto s = four();
    for (int bar = 0; bar < 64; ++bar)
    {
        const auto winner = ownerSlot (s, bar);
        int fullDensity = 0;
        for (int slot = 1; slot <= 4; ++slot)
        {
            const auto scale = yieldFor (winner == slot, 100.0f);
            fullDensity += scale.density == 1.0f ? 1 : 0;
            GF_EXPECT (scale.density == (winner == slot ? 1.0f : 0.0f));
        }
        GF_EXPECT (fullDensity == 1);
    }
}

GF_TEST (CoopDeterministic, priority_share_is_within_three_percentage_points_over_2000_slots)
{
    auto s = four();
    s.members[0].priority = 10;
    s.members[1].priority = 20;
    s.members[2].priority = 30;
    s.members[3].priority = 40;
    int counts[4] {};
    for (int i = 0; i < 2000; ++i)
        ++counts[ownerSlot (s, i) - 1];
    for (int i = 0; i < 4; ++i)
        GF_EXPECT (std::abs ((double) counts[i] / 2000.0 - 0.1 * (i + 1)) <= 0.03);
}

GF_TEST (CoopDeterministic, removing_a_non_owner_never_changes_the_owner)
{
    const auto s = four();
    auto reduced = s;
    reduced.count = 3;
    for (int i = -1000; i < 1000; ++i)
        if (ownerSlot (s, i) != 4)
            GF_EXPECT (ownerSlot (reduced, i) == ownerSlot (s, i));
}

GF_TEST (CoopDeterministic, election_uses_fixed_slots_not_runtime_claim_order)
{
    const auto a = four();
    auto b = a;
    std::reverse (b.members.begin(), b.members.begin() + b.count);
    for (int i = 0; i < b.count; ++i)
        b.members[(std::size_t) i].token += 10000;
    for (int i = -1000; i < 1000; ++i)
        GF_EXPECT (ownerSlot (a, i) == ownerSlot (b, i));
}

GF_TEST (CoopDeterministic, block_patterns_and_one_or_four_threads_produce_identical_elections)
{
    const auto s = four();
    std::array<std::uint8_t, 2048> reference {}, parallel {};
    for (std::size_t i = 0; i < reference.size(); ++i)
        reference[i] = ownerSlot (s, (std::int64_t) i);
    std::thread threads[4];
    for (int t = 0; t < 4; ++t)
        threads[t] = std::thread ([&, t]
        {
            const int block = t & 1 ? 64 : 512;
            for (std::size_t i = (std::size_t) t; i < parallel.size(); i += 4)
            {
                const double decisionPpq = (double) i * 4.0 + 0.5;
                const double hostStart = std::floor (decisionPpq * 24000.0 / block) * block;
                const auto sampleOffset = decisionPpq * 24000.0 - hostStart;
                std::int64_t index = 0;
                if (slotIndex ((hostStart + sampleOffset) / 24000.0, Grid::Bar, 4, 4, index))
                    parallel[i] = ownerSlot (s, index);
            }
        });
    for (auto& thread : threads)
        thread.join();
    GF_EXPECT (parallel == reference);
}

GF_TEST (CoopDeterministic, invalid_or_duplicate_members_are_rejected)
{
    auto s = four();
    GF_EXPECT (valid (s));
    s.members[1].slot = 1;
    GF_EXPECT (! valid (s));
    GF_EXPECT (ownerSlot (s, 0) == 0);
    s = four();
    s.members[1].token = s.members[0].token;
    GF_EXPECT (! valid (s));
    s = four();
    s.count = 33;
    GF_EXPECT (! valid (s));
}

GF_TEST (CoopDeterministic, meter_grid_and_negative_preroll_are_sample_position_based)
{
    std::int64_t index = 0;
    GF_EXPECT (slotIndex (3.5, Grid::Bar, 7, 8, index) && index == 1);
    GF_EXPECT (slotIndex (-0.1, Grid::Bar, 4, 4, index) && index == -1);
    GF_EXPECT (slotIndex (8.0, Grid::TwoBars, 4, 4, index) && index == 1);
    GF_EXPECT (slotIndex (8.0, Grid::FourBars, 4, 4, index) && index == 0);
    GF_EXPECT (slotIndex (1.5, Grid::Beat, 4, 4, index) && index == 1);
    GF_EXPECT (slotIndex (1.5, Grid::TwoBeats, 4, 4, index) && index == 0);
    GF_EXPECT (! slotIndex (std::numeric_limits<double>::infinity(), Grid::Bar, 4, 4, index));
}

GF_TEST (CoopDeterministic, yield_only_reduces_non_owner_values_and_never_draws_rng)
{
    for (float yield : { -10.0f, 0.0f, 50.0f, 100.0f, 200.0f })
    {
        const auto owner = yieldFor (true, yield);
        const auto other = yieldFor (false, yield);
        GF_EXPECT (owner.density == 1.0f && owner.fill == 1.0f);
        GF_EXPECT (other.density >= 0.0f && other.density <= 1.0f);
        GF_EXPECT (other.fill >= 0.5f && other.fill <= 1.0f);
    }
    GF_EXPECT (yieldFor (false, 50.0f).density == 0.5f);
    GF_EXPECT (yieldFor (false, 50.0f).fill == 0.75f);
}

GF_TEST (CoopDeterministic, last_ulp_host_rounding_does_not_change_bar_ownership)
{
    std::int64_t before = 0, exact = 0, after = 0;
    GF_EXPECT (slotIndex (std::nextafter (8.0, 0.0), Grid::Bar, 4, 4, before));
    GF_EXPECT (slotIndex (8.0, Grid::Bar, 4, 4, exact));
    GF_EXPECT (slotIndex (std::nextafter (8.0, 16.0), Grid::Bar, 4, 4, after));
    GF_EXPECT (before == exact && after == exact);
    // The rounding allowance must stay below a sample, not swallow real early positions.
    GF_EXPECT (slotIndex (8.0 - 1.0e-6, Grid::Bar, 4, 4, before) && before == 1);
}

#if defined (GF_COOP_STANDALONE)
int main()
{
    int failed = 0;
    for (const auto& [name, run] : cooptest::cases())
        try { run(); }
        catch (const std::exception& e) { ++failed; std::cerr << name << ": " << e.what() << '\n'; }
    std::cout << cooptest::cases().size() - (std::size_t) failed << " passed, " << failed << " failed\n";
    return failed != 0 ? 1 : 0;
}
#endif
