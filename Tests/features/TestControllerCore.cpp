#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "features/controller/FeedbackCore.h"
#include "features/controller/Takeover.h"
#include <array>
#include <limits>

using namespace gapfill::controller;
using namespace gftest;
namespace
{
    struct Output
    {
        std::array<OutEvent, 2048> events {};
        std::array<double, 2048> times {};
        std::size_t count = 0;
        double now = 0.0;
        bool connected = true;
        static bool send (void* context, OutEvent event) noexcept
        {
            auto& out = *static_cast<Output*> (context);
            if (! out.connected || out.count == out.events.size()) return false;
            out.events[out.count] = event; out.times[out.count++] = out.now;
            return true;
        }
        int tick (FeedbackCore& core, double ms)
        { now = ms; return core.tick (ms, send, this); }
    };
}
GF_TEST (ControllerCore, pickup_waits_for_crossing_and_rearms_after_external_parameter_changes)
{
    Takeover pickup;
    GF_EXPECT (! pickup.accepts (TakeoverMode::Pickup, 0.1f, 0.7f));
    GF_EXPECT (! pickup.accepts (TakeoverMode::Pickup, 0.5f, 0.7f));
    GF_EXPECT (pickup.accepts (TakeoverMode::Pickup, 0.8f, 0.7f));
    pickup.applied (0.8f);
    GF_EXPECT (pickup.accepts (TakeoverMode::Pickup, 0.6f, 0.8f));
    pickup.applied (0.6f);
    GF_EXPECT (! pickup.accepts (TakeoverMode::Pickup, 0.5f, 0.9f));
    GF_EXPECT (pickup.accepts (TakeoverMode::Pickup, 0.89f, 0.9f));
    GF_EXPECT (pickup.accepts (TakeoverMode::Jump, 0.0f, 1.0f));
    pickup.reset();
    GF_EXPECT (! pickup.accepts (TakeoverMode::Pickup, 0.0f, 1.0f));
}
GF_TEST (ControllerCore, changed_values_are_rate_limited_per_address_and_unchanged_values_are_silent)
{
    FeedbackCore core; Output out;
    const std::array<OutEvent, 1> addresses {{{0xb0, 74, 0}}};
    GF_EXPECT (core.configure (addresses));
    for (int t = 0; t < 1000; ++t)
    { core.setValue (0, (std::uint8_t) (t % 128)); out.tick (core, t); }
    GF_EXPECT (out.count <= 30 && out.count >= 20);
    const auto previous = out.count;
    core.setValue (0, out.events[out.count - 1].data2);
    for (int t = 1000; t < 1500; ++t) out.tick (core, t);
    GF_EXPECT (out.count == previous);
}
GF_TEST (ControllerCore, full_resync_finishes_inside_200ms_and_total_never_exceeds_512_in_a_second)
{
    FeedbackCore core; Output out;
    std::array<OutEvent, kMaxFeedbackOutputs> addresses {};
    for (std::size_t i = 0; i < addresses.size(); ++i) addresses[i] = {0xb0, (std::uint8_t) i, 0};
    GF_EXPECT (core.configure (addresses));
    for (int tick = 0; tick <= 5; ++tick) out.tick (core, tick * (1000.0 / 30.0));
    GF_EXPECT (out.count == addresses.size() && out.times[out.count - 1] <= 200.0);
    out.count = 0; core.resync (200.0);
    for (int t = 200; t < 2200; ++t)
    {
        for (std::size_t i = 0; i < addresses.size(); ++i) core.setValue (i, (std::uint8_t) (t % 128));
        out.tick (core, t);
    }
    for (std::size_t i = 0; i < out.count; ++i)
    {
        std::size_t inWindow = 0;
        for (std::size_t j = i; j < out.count && out.times[j] - out.times[i] < 1000.0; ++j) ++inWindow;
        GF_EXPECT (inWindow <= 512);
    }
}
GF_TEST (ControllerCore, ten_second_loopback_stays_suppressed_and_settles_within_300ms)
{
    FeedbackCore core; Output out;
    const std::array<OutEvent, 1> addresses {{{0xb1, 74, 0}}};
    GF_EXPECT (core.configure (addresses));
    for (int t = 0; t < 10000; t += 10)
    {
        core.noteInput (2, 74, t); core.setValue (0, (std::uint8_t) ((t / 10) % 128)); out.tick (core, t);
    }
    GF_EXPECT (out.count == 0);
    for (int t = 10000; t < 10300; ++t) out.tick (core, t);
    GF_EXPECT (out.count == 1);
    const auto settled = out.count;
    for (int t = 10300; t < 11000; ++t) out.tick (core, t);
    GF_EXPECT (out.count == settled);
}
GF_TEST (ControllerCore, disconnect_retains_latest_values_for_a_bounded_reconnect_resync)
{
    FeedbackCore core; Output out;
    const std::array<OutEvent, 2> addresses {{{0xb0, 70, 10}, {0x90, 36, 127}}};
    GF_EXPECT (core.configure (addresses)); out.connected = false;
    GF_EXPECT (out.tick (core, 0.0) == 0);
    core.setValue (0, 99); out.connected = true; core.resync (100.0);
    GF_EXPECT (out.tick (core, 100.0) == 2 && out.events[0].data2 == 99);
}
GF_TEST (ControllerCore, malformed_configuration_is_atomic_and_every_update_is_allocation_free)
{
    FeedbackCore core; Output out;
    const std::array<OutEvent, 1> addresses {{{0xb0, 70, 0}}};
    GF_EXPECT (core.configure (addresses));
    const std::array<OutEvent, 2> duplicates {{{0xb0, 70, 0}, {0xb0, 70, 1}}};
    GF_EXPECT (! core.configure (duplicates) && core.size() == 1);
    const std::array<OutEvent, 1> invalid {{{0xc0, 70, 0}}};
    GF_EXPECT (! core.configure (invalid) && core.size() == 1);
    AllocGuard guard;
    for (int t = 0; t < 10000; ++t)
    { core.setValue (0, (std::uint8_t) (t % 128)); core.noteInput (1, 70, t); out.tick (core, t); }
    GF_EXPECT (guard.count() == 0);
}
