// F-M21: complete published curve snapshots, including concurrent host restores.
#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "../../Source/features/tension/Tension.h"

#include <array>
#include <atomic>
#include <barrier>
#include <cstring>
#include <thread>

using namespace gapfill::tension;

namespace
{
    TensionCurves markedCurves (int marker)
    {
        TensionCurves result;
        for (int lane = 0; lane < kLanes; ++lane)
        {
            auto& value = result.lane[lane];
            value.numPoints = kMaxPoints;
            value.interp = (std::uint8_t) (lane % 3);
            for (int point = 0; point < kMaxPoints; ++point)
                value.pt[point] = { (std::uint16_t) (point * 1000 / (kMaxPoints - 1)),
                                    (std::uint16_t) (marker + lane * 10 + point) };
        }
        return result;
    }

    bool identical (const TensionCurves& a, const TensionCurves& b)
    {
        return std::memcmp (&a, &b, sizeof a) == 0;
    }
}

GF_TEST (TensionPublication, default_and_valid_curves_keep_every_point_exactly)
{
    TensionService service;
    GF_EXPECT (identical (service.curves(), TensionCurves {}));
    GF_EXPECT (service.isDefault());
    for (int marker = 0; marker <= 900; marker += 9)
    {
        const auto expected = markedCurves (marker);
        GF_EXPECT (validate (expected));
        GF_EXPECT (service.publishCurves (expected));
        GF_EXPECT (identical (service.curves(), expected));
    }
    const auto expected = markedCurves (321);
    auto fewerPoints = expected;
    for (auto& lane : fewerPoints.lane)
    {
        lane.numPoints = 2;
        lane.pt[1].x = 1000;
    }
    GF_EXPECT (service.publishCurves (fewerPoints));
    GF_EXPECT (identical (service.curves(), fewerPoints)); // inactive points are preserved, too
    GF_EXPECT (! service.isDefault());
    GF_EXPECT (service.publishCurves (TensionCurves {}));
    GF_EXPECT (service.isDefault());
}

GF_TEST (TensionPublication, invalid_publication_keeps_the_previous_complete_value)
{
    TensionService service;
    const auto expected = markedCurves (250);
    GF_EXPECT (service.publishCurves (expected));
    auto invalid = expected;
    invalid.lane[2].pt[3].x = invalid.lane[2].pt[2].x;
    GF_EXPECT (! service.publishCurves (invalid));
    GF_EXPECT (identical (service.curves(), expected));
    invalid = expected;
    invalid.lane[3].numPoints = 255;
    GF_EXPECT (! service.publishCurves (invalid));
    GF_EXPECT (identical (service.curves(), expected));
}

GF_TEST (TensionPublication, multiple_restore_writers_and_readers_never_observe_mixed_curves)
{
    TensionService service;
    const auto first = markedCurves (100), second = markedCurves (800);
    const TensionCurves neutral;
    std::atomic<bool> coherent { true };
    std::barrier start (7); // two non-RT writers, four readers, and this owner
    std::array<std::thread, 2> writers;
    std::array<std::thread, 4> readers;
    for (int writer = 0; writer < 2; ++writer)
        writers[(std::size_t) writer] = std::thread ([&, writer]
        {
            start.arrive_and_wait();
            for (int iteration = 0; iteration < 20000; ++iteration)
                if (! service.publishCurves (writer == 0 ? first : second))
                    coherent.store (false, std::memory_order_relaxed);
        });
    for (auto& reader : readers)
        reader = std::thread ([&]
        {
            start.arrive_and_wait();
            for (int iteration = 0; iteration < 40000; ++iteration)
            {
                const auto copy = service.curves();
                // A busy publisher may produce the existing neutral fallback.
                // A successful snapshot must contain all lanes of one value.
                if (! identical (copy, first) && ! identical (copy, second) && ! identical (copy, neutral))
                    coherent.store (false, std::memory_order_relaxed);
            }
        });
    start.arrive_and_wait();
    for (auto& writer : writers) writer.join();
    for (auto& reader : readers) reader.join();
    GF_EXPECT (coherent.load());
    const auto final = service.curves();
    GF_EXPECT (identical (final, first) || identical (final, second));
}

GF_TEST (TensionPublication, repeated_reader_is_allocation_free_and_does_not_mutate_status)
{
    TensionService service;
    const auto expected = markedCurves (500);
    GF_EXPECT (service.publishCurves (expected));
    TensionOut status;
    status.phase = 0.375f;
    status.active = true;
    service.setStatus (status);
    bool unchanged = true;
    long allocations;
    {
        gftest::AllocGuard guard;
        for (int iteration = 0; iteration < 10000; ++iteration)
            unchanged = unchanged && identical (service.curves(), expected) && ! service.isDefault();
        allocations = guard.count();
    }
    GF_EXPECT (unchanged && allocations == 0);
    GF_EXPECT (service.lastActive());
    GF_EXPECT_NEAR (service.lastPhase(), status.phase, 0.0);
}
