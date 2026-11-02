// F-M21: complete published curve snapshots, including concurrent host restores.
#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "../../Source/features/tension/Tension.h"
#include "../../Source/features/tension/TensionTree.h"

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

    struct HeldPublisher
    {
        std::barrier<> entered { 2 }, finish { 2 };
        static void hold (void* context)
        {
            auto& state = *static_cast<HeldPublisher*> (context);
            state.entered.arrive_and_wait();
            state.finish.arrive_and_wait();
        }
    };
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

GF_TEST (TensionPublication, strict_busy_capture_keeps_output_while_audio_retains_flat_fallback)
{
    TensionService service;
    const auto previous = markedCurves (100), next = markedCurves (800);
    GF_EXPECT (service.publishCurves (previous));
    HeldPublisher paused;
    service.setAfterPublishBeginForTests (&HeldPublisher::hold, &paused);
    bool published = false;
    std::thread writer ([&] { published = service.publishCurves (next); });
    paused.entered.arrive_and_wait();
    auto output = previous;
    const bool captured = service.tryCurves (output);
    const auto audio = service.curves();
    paused.finish.arrive_and_wait();
    writer.join();
    service.setAfterPublishBeginForTests (nullptr, nullptr);
    GF_EXPECT (! captured && identical (output, previous));
    GF_EXPECT (identical (audio, TensionCurves {}));
    GF_EXPECT (published && service.tryCurves (output) && identical (output, next));
}

GF_TEST (TensionPublication, busy_serialization_is_an_error_and_never_an_omitted_flat_subtree)
{
    TensionService service;
    GF_EXPECT (service.publishCurves (markedCurves (100)));
    HeldPublisher paused;
    service.setAfterPublishBeginForTests (&HeldPublisher::hold, &paused);
    std::thread writer ([&] { service.publishCurves (markedCurves (800)); });
    paused.entered.arrive_and_wait();
    const auto busy = serialiseTree (service);
    paused.finish.arrive_and_wait();
    writer.join();
    service.setAfterPublishBeginForTests (nullptr, nullptr);
    GF_EXPECT (busy.isValid() && busy.hasType ("Tension") && busy.hasProperty ("__serialiseError"));
    const auto captured = serialiseTree (service);
    GF_EXPECT (captured.isValid() && ! captured.hasProperty ("__serialiseError") && validateTree (captured));
    GF_EXPECT (captured.getChild (0).getProperty ("pts").toString().startsWith ("0:800,"));
    GF_EXPECT (captured.getChild (3).getProperty ("pts").toString().endsWith ("1000:845"));
    GF_EXPECT (service.publishCurves (TensionCurves {}));
    GF_EXPECT (! serialiseTree (service).isValid()); // proved flat by a successful capture
}
