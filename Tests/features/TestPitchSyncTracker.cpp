#if defined (GF_CONTEXT_STANDALONE)
 #include "ContextStandalone.h"
 #include <iostream>
#else
 #include "../TestFramework.h"
 #include "../support/AllocGuard.h"
#endif
#include "../../Source/features/harmonic/psola/PitchTracker.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <vector>
using namespace gapfill::harmonic::psola;
namespace
{
    std::vector<float> tone (double sr, double hz, double seconds, float noise = 0)
    {
        std::vector<float> x ((std::size_t) (sr * seconds));
        std::uint32_t random = 7123;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            random = random * 1664525u + 1013904223u;
            const float n = (float) (random >> 8) / 8388608.0f - 1;
            const double phase = 6.283185307179586 * hz * (double) i / sr;
            x[i] = (float) (0.2 * std::sin (phase) + 0.12 * std::sin (phase * 2) + 0.06 * std::sin (phase * 3)) + noise * n;
        }
        return x;
    }
    void feed (PitchTracker& tracker, const std::vector<float>& x, int block, std::int64_t start = 0)
    {
        for (std::size_t i = 0; i < x.size();)
        {
            const int n = (int) std::min<std::size_t> ((std::size_t) block, x.size() - i);
            tracker.push (x.data() + i, n, start + (std::int64_t) i);
            i += (std::size_t) n;
        }
    }
}
GF_TEST (PitchSyncTracker, inactive_is_empty_and_each_hop_has_a_fixed_work_ceiling)
{
    PitchTracker tracker;
    tracker.prepare (48000);
    const auto x = tone (48000, 220, 0.5);
    feed (tracker, x, 4096);
    GF_EXPECT (tracker.totalFrames() == 0 && tracker.maxMacsPerHop() == 0);
    tracker.setActive (true);
    feed (tracker, x, 4096);
    GF_EXPECT (tracker.totalFrames() > 10 && tracker.maxMacsPerHop() <= 8192 && tracker.maxLagsPerHop() <= 16);
    GF_EXPECT (sizeof (tracker) <= 40 * 1024);
}
GF_TEST (PitchSyncTracker, four_rate_noisy_voiced_frames_meet_twenty_cent_accuracy)
{
    int voiced = 0, accurate = 0, count = 0, octaveErrors = 0;
    double worstCents = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double hz : { 55.0, 110.0, 220.0, 440.0, 880.0, 1000.0 })
        {
            PitchTracker tracker;
            tracker.prepare (sr);
            tracker.setActive (true);
            const auto x = tone (sr, hz, 0.75, 0.02f);
            feed (tracker, x, 480);
            for (int i = 0; i < tracker.storedFrames(); ++i)
            {
                const auto& frame = tracker.frame (i);
                if (frame.histPos < (std::int64_t) (sr * 0.2)) continue;
                ++count;
                voiced += frame.f0Hz > 0 ? 1 : 0;
                if (frame.f0Hz > 0)
                {
                    const double cents = std::abs (1200 * std::log2 (frame.f0Hz / hz));
                    accurate += cents <= 20 ? 1 : 0;
                    octaveErrors += cents >= 600 ? 1 : 0;
                    worstCents = std::max (worstCents, cents);
                }
            }
            const auto query = tracker.query ((std::int64_t) x.size() - (std::int64_t) (0.32 * sr), (std::int64_t) x.size() - 1);
            GF_EXPECT (query.voiced && std::abs (1200 * std::log2 (query.f0Hz / hz)) <= 20);
        }
    std::printf (" noisy tracker frames%d voiced%d accurate%d octaveErrors%d worst%.3fc\n", count, voiced, accurate, octaveErrors, worstCents);
    GF_EXPECT (count > 100 && voiced * 20 >= count * 19 && accurate * 20 >= voiced * 19);
    GF_EXPECT (octaveErrors * 50 < voiced);
}
GF_TEST (PitchSyncTracker, noise_pink_kicks_and_hats_are_ninety_percent_unvoiced)
{
    for (int kind = 0; kind < 4; ++kind)
    {
        std::vector<float> source (96000);
        std::uint32_t random = 78123;
        float pink = 0;
        for (int i = 0; i < 96000; ++i)
        {
            random = random * 1664525u + 1013904223u;
            const float noise = (float) (random >> 8) / 8388608.0f - 1;
            pink += 0.025f * (noise - pink);
            const double phase = std::fmod (i / 48000.0, kind == 3 ? 0.125 : 0.5);
            source[(std::size_t) i] = kind == 0 ? noise * 0.2f : kind == 1 ? pink
                : kind == 2 ? (float) (0.3 * std::exp (-phase * 18) * std::sin (6.283185307179586 * 55 * phase))
                            : (float) (0.3 * noise * std::exp (-phase * 80));
        }
        PitchTracker tracker; tracker.prepare (48000); tracker.setActive (true);
        feed (tracker, source, 512);
        int frames = 0, unvoiced = 0;
        for (int i = 0; i < tracker.storedFrames(); ++i)
        {
            const auto& value = tracker.frame (i);
            if (value.histPos < 9600) continue;
            ++frames;
            unvoiced += value.f0Hz == 0 ? 1 : 0;
        }
        std::printf (" unvoiced source%d: %d/%d\n", kind, unvoiced, frames);
        GF_EXPECT (frames > 100 && unvoiced * 10 >= frames * 9);
    }
}
GF_TEST (PitchSyncTracker, frames_and_source_coordinates_are_identical_for_four_chunk_sizes)
{
    const auto x = tone (48000, 147, 1.0);
    PitchTracker baseline;
    baseline.prepare (48000); baseline.setActive (true); feed (baseline, x, 32, 123456);
    for (int block : { 64, 480, 4096 })
    {
        PitchTracker candidate;
        candidate.prepare (48000); candidate.setActive (true); feed (candidate, x, block, 123456);
        GF_EXPECT (baseline.totalFrames() == candidate.totalFrames() && baseline.storedFrames() == candidate.storedFrames());
        for (int i = 0; i < baseline.storedFrames(); ++i)
        {
            const auto& a = baseline.frame (i); const auto& b = candidate.frame (i);
            GF_EXPECT (a.histPos == b.histPos && a.f0Hz == b.f0Hz && a.clarity == b.clarity);
        }
        GF_EXPECT (candidate.maxMacsPerHop() <= 8192);
    }
}
GF_TEST (PitchSyncTracker, vibrato_tracks_within_fifty_cents_at_its_source_timestamp)
{
    std::vector<float> input (48000);
    double phase = 0;
    for (int i = 0; i < 48000; ++i)
    {
        const double frequency = 440 * std::exp2 (25 * std::sin (6.283185307179586 * 5 * i / 48000) / 1200);
        phase += 6.283185307179586 * frequency / 48000;
        input[(std::size_t) i] = (float) (0.2 * std::sin (phase) + 0.1 * std::sin (phase * 2));
    }
    PitchTracker tracker; tracker.prepare (48000); tracker.setActive (true);
    feed (tracker, input, 512);
    int checked = 0, good = 0;
    for (int i = 0; i < tracker.storedFrames(); ++i)
    {
        const auto& value = tracker.frame (i);
        if (value.histPos < 9600) continue;
        ++checked;
        const double expected = 440 * std::exp2 (25 * std::sin (6.283185307179586 * 5 * (double) value.histPos / 48000) / 1200);
        good += value.f0Hz > 0 && std::abs (1200 * std::log2 (value.f0Hz / expected)) <= 50 ? 1 : 0;
    }
    GF_EXPECT (checked > 50 && good * 20 >= checked * 19);
}
GF_TEST (PitchSyncTracker, capture_pause_keeps_coordinates_and_a_discontinuity_relearns)
{
    PitchTracker tracker; tracker.prepare (48000); tracker.setActive (true);
    const auto x = tone (48000, 220, 0.5);
    feed (tracker, x, 64, 100000);
    const auto a = tracker.query (110000, 123999);
    const auto frames = tracker.totalFrames();
    const auto held = tracker.query (110000, 123999); // no history.write means no push
    GF_EXPECT (a.voiced && held.f0Hz == a.f0Hz && tracker.totalFrames() == frames);
    tracker.push (x.data(), 32, 200000); // actual source reset/gap invalidates old positions
    GF_EXPECT (! tracker.query (110000, 123999).voiced && tracker.totalFrames() == 0);
    tracker.setActive (false);
    GF_EXPECT (! tracker.isActive() && tracker.storedFrames() == 0);
}
GF_TEST (PitchSyncTracker, invalid_samples_and_position_overflow_drop_stale_pitch)
{
    PitchTracker tracker; tracker.prepare (48000); tracker.setActive (true);
    const auto x = tone (48000, 220, 0.5);
    feed (tracker, x, 512);
    const float invalid[] { std::numeric_limits<float>::quiet_NaN() };
    tracker.push (invalid, 1, (std::int64_t) x.size());
    GF_EXPECT (tracker.storedFrames() == 0 && tracker.totalFrames() == 0);
    tracker.push (x.data(), 512, std::numeric_limits<std::int64_t>::max() - 10);
    GF_EXPECT (tracker.storedFrames() == 0);
    tracker.prepare (192000);
    GF_EXPECT (! tracker.isActive() && tracker.decimation() == 16 && tracker.totalFrames() == 0);
}
GF_TEST (PitchSyncTracker, reference_tracker_total_cpu_budget_is_not_relaxed)
{
    const auto x = tone (48000, 110, 1.0);
    PitchTracker tracker; tracker.prepare (48000); tracker.setActive (true);
    const auto started = std::clock();
    feed (tracker, x, 32);
    const double cpu = (double) (std::clock() - started) / CLOCKS_PER_SEC;
    std::printf (" incremental tracker CPU%.4f%% of one core (limit0.5%%)\n", cpu * 100);
    const auto* flag = std::getenv ("GAPFILL_PERF_ENFORCE");
    GF_EXPECT (flag == nullptr || std::strcmp (flag, "1") != 0 || cpu <= 0.005);
}
#if ! defined (GF_CONTEXT_STANDALONE)
GF_TEST (PitchSyncTracker, push_query_and_reset_do_not_allocate_on_audio)
{
    const auto x = tone (48000, 220, 1.0);
    PitchTracker tracker; tracker.prepare (48000); tracker.setActive (true);
    long allocations = 0;
    PitchTracker::Query result;
    {
        gftest::AllocGuard guard;
        feed (tracker, x, 512);
        result = tracker.query (10000, 47999);
        tracker.reset();
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
