#if defined (GF_CONTEXT_STANDALONE)
 #include "ContextStandalone.h"
 #include <iostream>
#else
 #include "../TestFramework.h"
 #include "../support/AllocGuard.h"
#endif
#include "../../Source/features/tail/TailPrediction.h"
#include <atomic>
#include <limits>
#include <thread>
using namespace gapfill::tail;
GF_TEST (TailPrediction, all_fx_off_preserves_the_exact_released_host_tail)
{
    const PublishedBounds prepared;
    GF_EXPECT (predictSeconds (prepared.load(), {}) == 2.5);
    PreparedBounds invalid; invalid.valid = false;
    GF_EXPECT (predictSeconds (invalid, {}) == 2.5);
}
GF_TEST (TailPrediction, automation_before_the_first_callback_uses_prepared_caps)
{
    PublishedBounds prepared;
    Activation snapshot;
    snapshot.legacy[(std::size_t) LegacyStage::Dub] = true;
    GF_EXPECT (predictSeconds (prepared.load(), snapshot) == 11.0);
    PreparedBounds configured;
    configured.legacySeconds[(std::size_t) LegacyStage::Dub] = 16.25 + 256.0 / 48000;
    GF_EXPECT (prepared.publish (configured));
    const double bound = predictSeconds (prepared.load(), snapshot);
    GF_EXPECT (bound >= 18.75 + 256.0 / 48000 && bound < 18.75 + 256.0 / 48000 + 0.0625);
    snapshot.legacy[(std::size_t) LegacyStage::Dub] = false;
    GF_EXPECT (predictSeconds (prepared.load(), snapshot) == 2.5);
}
GF_TEST (TailPrediction, cascaded_legacy_effects_add_their_complete_tail_extents)
{
    PreparedBounds bounds;
    bounds.legacySeconds = { 0.03125, 2.25, 4.25, 8.25, 1.75, 4.78125 };
    Activation active;
    active.legacy[(std::size_t) LegacyStage::Shift] = true;
    active.legacy[(std::size_t) LegacyStage::Dub] = true;
    GF_EXPECT (predictSeconds (bounds, active) == 15.0);
    active.legacy[(std::size_t) LegacyStage::Convolution] = true;
    GF_EXPECT (predictSeconds (bounds, active) == 16.75);
    // A serial chain can ring after the preceding stage has stopped. Taking
    // only max(8.25,4.25) would under-report this valid legacy topology.
    GF_EXPECT (predictSeconds (bounds, active) > kBaseSeconds + 8.25);
    active.parallelMaximumSeconds = 6.5;
    GF_EXPECT (predictSeconds (bounds, active) == 23.25);
}
GF_TEST (TailPrediction, parallel_returns_use_one_maximum_and_do_not_change_dry_latency)
{
    const PreparedBounds bounds;
    Activation active;
    active.parallelMaximumSeconds = std::max ({ 6.5, 6.5, 0.85 });
    GF_EXPECT (predictSeconds (bounds, active) == 9.0);
    active.parallelMaximumSeconds = 0.85;
    GF_EXPECT (predictSeconds (bounds, active) == 3.35);
    GF_EXPECT (sizeof (PublishedBounds) == 8); // host metadata only, no audio buffers
}
GF_TEST (TailPrediction, quantisation_is_always_upward_at_all_supported_rate_bounds)
{
    for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        PreparedBounds source;
        const int window = rate <= 48000 ? 1024 : 2048;
        source.legacySeconds = { 2.0 * (window - 1) / rate, 2.25 + 256 / rate,
            4.25 + 256 / rate, 16.25 + 256 / rate, 1.75 + 256 / rate,
            4.75 + 2.0 * (window - 1) / rate + 256 / rate };
        PublishedBounds published; GF_EXPECT (published.publish (source));
        const auto actual = published.load();
        for (std::size_t i = 0; i < kLegacyStages; ++i)
            GF_EXPECT (actual.legacySeconds[i] >= source.legacySeconds[i]
                && actual.legacySeconds[i] - source.legacySeconds[i] < 0.0625);
    }
}
GF_TEST (TailPrediction, malformed_prepared_bounds_fail_conservatively_and_remain_finite)
{
    Activation active; active.legacy[(std::size_t) LegacyStage::Dub] = true;
    for (double value : { -1.0, 32.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
    {
        PreparedBounds bounds; bounds.legacySeconds[3] = value;
        PublishedBounds published;
        GF_EXPECT (! published.publish (bounds));
        GF_EXPECT (predictSeconds (published.load(), active) == kInvalidHostBoundSeconds);
    }
    active.parallelMaximumSeconds = std::numeric_limits<double>::quiet_NaN();
    GF_EXPECT (predictSeconds ({}, active) == kInvalidHostBoundSeconds);
}
GF_TEST (TailPrediction, host_queries_never_observe_torn_preparation_snapshots)
{
    PreparedBounds first, second;
    first.legacySeconds = { 1, 2, 3, 4, 5, 6 };
    second.legacySeconds = { 10, 11, 12, 13, 14, 15 };
    PublishedBounds published; published.publish (first);
    std::atomic<bool> done { false }, bad { false };
    std::thread publisher ([&]
    {
        for (int i = 0; i < 20000; ++i) published.publish ((i & 1) != 0 ? first : second);
        done.store (true, std::memory_order_release);
    });
    while (! done.load (std::memory_order_acquire))
    {
        const auto actual = published.load();
        if (! actual.valid || (actual.legacySeconds != first.legacySeconds && actual.legacySeconds != second.legacySeconds)) bad.store (true);
    }
    publisher.join();
    GF_EXPECT (! bad.load());
}
#if ! defined (GF_CONTEXT_STANDALONE)
GF_TEST (TailPrediction, publication_and_host_query_do_not_allocate)
{
    PublishedBounds prepared;
    Activation active; active.legacy.fill (true); active.parallelMaximumSeconds = 6.5;
    long allocations = 0; double result = 0;
    {
        gftest::AllocGuard guard;
        for (int i = 0; i < 10000; ++i) { prepared.publish ({}); result = predictSeconds (prepared.load(), active); }
        allocations = guard.count();
    }
    GF_EXPECT (allocations == 0 && result > 16.25);
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
