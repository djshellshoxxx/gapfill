// g++ -std=c++20 -O3 -Wall -Wextra -Wconversion -Wshadow -Werror -DGF_CONTEXT_STANDALONE
//   Tests/features/TestArrangementPrediction.cpp Source/features/arrangement/{SectionTracker,Modulators}.cpp -o /tmp/arrangement
#if defined (GF_CONTEXT_STANDALONE)
 #include "ContextStandalone.h"
 #include <iostream>
#else
 #include "../TestFramework.h"
#endif
#include "../../Source/features/arrangement/Modulators.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
using namespace gapfill;
using namespace gapfill::arrangement;
namespace
{
    context::FrameFeatures frameFor (int section, int frame = 0, bool varying = false)
    {
        context::FrameFeatures f;
        f.rmsDb = -40.0f + (float) (section % 3) * 12.0f;
        f.onsetsPerSec = 2.0f + (float) (section % 3) * 3.0f;
        f.bandShare[0] = 0.1f + (varying ? (float) (frame % 7) * 0.03f : (float) (section % 3) * 0.1f);
        f.bandShare[1] = 0.3f;
        f.bandShare[2] = 1.0f - f.bandShare[0] - f.bandShare[1];
        f.brightness = f.bandShare[2] + 0.5f * f.bandShare[1];
        return f;
    }
    void hashWord (std::uint64_t& hash, std::uint32_t word)
    {
        for (int i = 0; i < 4; ++i) { hash ^= (word >> (i * 8)) & 255u; hash *= 1099511628211ull; }
    }
}
GF_TEST (ArrangementV2, released_tracker_trace_is_unchanged)
{
    SectionTracker tracker;
    tracker.reset();
    std::uint64_t hash = 1469598103934665603ull;
    for (int i = 0; i < 960; ++i)
    {
        tracker.pushFrame (frameFor (i / 32, i, true), 0.5f);
        const auto& s = tracker.state();
        for (auto value : { s.sectionIndex, (std::uint32_t) s.framesInto,
                           (std::uint32_t) (std::int32_t) s.expectedIn,
                           std::bit_cast<std::uint32_t> (s.level01), std::bit_cast<std::uint32_t> (s.energy01),
                           std::bit_cast<std::uint32_t> (s.trend), std::bit_cast<std::uint32_t> (s.confidence),
                           (std::uint32_t) s.learning, (std::uint32_t) tracker.lastBoundaryFrame() })
            hashWord (hash, value);
    }
    // The full float-bit checksum is recorded from the separate GCC/Linux
    // release probe. Native cross-platform tests keep stable timing/level
    // assertions; libm float bits are covered by per-platform audio goldens.
   #if defined (GF_CONTEXT_STANDALONE)
    GF_EXPECT (hash == 7771648117034845698ull);
   #else
    (void) hash;
   #endif
    GF_EXPECT (tracker.boundaries() == 27 && tracker.lastBoundaryFrame() == 927);
    GF_EXPECT (tracker.state().sectionIndex == 27 && tracker.state().framesInto == 33 && tracker.state().expectedIn == 0);
    GF_EXPECT (std::abs (tracker.state().level01 - 0.693182f) < 1e-5f);
    GF_EXPECT (std::abs (tracker.state().energy01 - 0.984848f) < 1e-5f);
}
GF_TEST (ArrangementV2, recurring_12_to_32_second_sections_predict_within_one_frame)
{
    for (const int period : { 24, 32, 48, 64 })
    {
        SectionTracker tracker;
        tracker.setModelVersion (2);
        int checked = 0, correct = 0;
        for (int i = 0; i < period * 12; ++i)
        {
            tracker.pushFrame (frameFor (i / period), 0.5f);
            // No arbitrary first-boundary promise. Once two full equal-length
            // sections have been heard, every frame counts, including unknowns.
            if (i >= period * 2 + 8)
            {
                const int truth = (period - (i + 1) % period) % period;
                ++checked;
                correct += tracker.state().expectedIn >= 0 && std::abs (tracker.state().expectedIn - truth) <= 1;
            }
        }
        std::printf ("  period %d: strict prediction %d/%d, boundaries %d\n", period, correct, checked, tracker.boundaries());
        GF_EXPECT (checked >= 200 && correct * 5 >= checked * 4);
        GF_EXPECT (tracker.boundaries() >= 10);
        GF_EXPECT (tracker.boundaryFrame (0) == period);
    }
}
GF_TEST (ArrangementV2, irregular_sections_do_not_invent_a_period)
{
    SectionTracker tracker;
    tracker.setModelVersion (2);
    constexpr int lengths[] { 20, 46, 28, 62, 18, 42, 26, 56 };
    int section = 0;
    for (int length : lengths)
    {
        for (int i = 0; i < length; ++i)
        {
            tracker.pushFrame (frameFor (section), 0.5f);
            GF_EXPECT (tracker.state().expectedIn == -1);
        }
        ++section;
    }
}
GF_TEST (ArrangementV2, version_switch_resets_history_and_invalid_versions_use_legacy)
{
    SectionTracker tracker;
    for (int i = 0; i < 80; ++i) tracker.pushFrame (frameFor (i / 24), 0.5f);
    tracker.setModelVersion (2);
    GF_EXPECT (tracker.frames() == 0 && tracker.state().learning && tracker.modelVersion() == 2);
    tracker.pushFrame (frameFor (1), 0.5f);
    tracker.setModelVersion (2);
    GF_EXPECT (tracker.frames() == 1);
    tracker.setModelVersion (17);
    GF_EXPECT (tracker.frames() == 0 && tracker.modelVersion() == 1);
}
GF_TEST (ArrangementV2, an_unconfirmed_period_does_not_predict_forever)
{
    SectionTracker tracker;
    tracker.setModelVersion (2);
    for (int i = 0; i < 24 * 4; ++i)
        tracker.pushFrame (frameFor (i / 24), 0.5f);
    for (int i = 0; i < 80; ++i)
        tracker.pushFrame (frameFor (3), 0.5f);
    GF_EXPECT (tracker.state().expectedIn == -1);
    GF_EXPECT (sizeof (SectionTracker) <= 80 * 1024);
}
GF_TEST (ArrangementV2, prediction_snaps_bars_phrases_and_fractional_time_signatures)
{
    ArrangementState s;
    s.expectedIn = 7;
    // 120 BPM: one frame is one quarter note. Predicted PPQ=7.2 -> bar8.
    GF_EXPECT (snapPrediction (s, true, 0.2, 120, 4, 4, 0).expectedIn == 8);
    // Phrase grid16 preferred over bars: 13.2 ->16.
    s.expectedIn = 13;
    GF_EXPECT (snapPrediction (s, true, 0.2, 120, 4, 4, 4).expectedIn == 16);
    // 6/8 means3 quarter notes per bar, 7/4 means7.
    s.expectedIn = 5;
    GF_EXPECT (snapPrediction (s, true, 0.0, 120, 6, 8, 0).expectedIn == 6);
    GF_EXPECT (snapPrediction (s, true, 0.0, 120, 7, 4, 0).expectedIn == 7);
    // Pre-roll uses the same absolute musical grid.
    GF_EXPECT (snapPrediction (s, true, -8.0, 120, 4, 4, 0).expectedIn == 4);
}
GF_TEST (ArrangementV2, invalid_transport_or_past_grid_target_is_unknown)
{
    ArrangementState s;
    s.expectedIn = 1;
    GF_EXPECT (snapPrediction (s, false, 0, 120, 4, 4, 0).expectedIn == -1);
    GF_EXPECT (snapPrediction (s, true, 0, 0, 4, 4, 0).expectedIn == -1);
    GF_EXPECT (snapPrediction (s, true, 0, 120, 0, 4, 0).expectedIn == -1);
    GF_EXPECT (snapPrediction (s, true, 0, 120, 4, 0, 0).expectedIn == -1);
    GF_EXPECT (snapPrediction (s, true, std::numeric_limits<double>::quiet_NaN(), 120, 4, 4, 0).expectedIn == -1);
    s.expectedIn = 0;
    GF_EXPECT (snapPrediction (s, true, 5.0, 120, 4, 4, 0).expectedIn == -1);
}
GF_TEST (ArrangementV2, calm_scales_the_whole_probability_without_a_density_floor)
{
    Fx04Settings s;
    s.on = true; s.calm01 = 1; s.settle01 = s.lift01 = 0;
    s.state.learning = false; s.state.confidence = 1; s.state.energy01 = 0; s.state.framesInto = 100;
    const auto legacy = evaluate (s, 120, 4);
    GF_EXPECT (std::abs (legacy.densityFactor - 0.2f) < 1e-6f && legacy.probabilityFactor == 1);
    s.modelVersion = 2;
    const auto modern = evaluate (s, 120, 4);
    GF_EXPECT (modern.densityFactor == 1 && std::abs (modern.probabilityFactor - 0.2f) < 1e-6f);
    for (float density : { 0.0f, 0.5f, 0.65f, 1.0f })
    {
        const float before = 0.4f + 0.6f * density;
        GF_EXPECT (before * modern.probabilityFactor <= before * 0.6f);
    }
    s.state.learning = true;
    const auto neutral = evaluate (s, 120, 4);
    GF_EXPECT (! neutral.active && neutral.probabilityFactor == 1 && neutral.densityFactor == 1);
}
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
