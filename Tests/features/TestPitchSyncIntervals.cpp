#if defined (GF_CONTEXT_STANDALONE)
 #include "ContextStandalone.h"
 #include <iostream>
#else
 #include "../TestFramework.h"
#endif
#include "../../Source/features/harmonic/psola/IntervalSets.h"
#include "../../Source/features/harmonic/psola/Random.h"
#include <cmath>
#include <limits>
using namespace gapfill::harmonic::psola;
GF_TEST (PitchSyncIntervals, default_off_is_empty_and_just_tables_are_exact)
{
    Settings settings;
    GF_EXPECT (intervalRatios (settings, 220, {}).count == 0);
    settings.enabled = true; settings.voices = 4; settings.intervals = IntervalSet::Major;
    auto ratios = intervalRatios (settings, 220, {});
    GF_EXPECT (ratios.count == 4 && ratios.values == (std::array<double,4> { 1,1.25,1.5,2 }));
    settings.intervals = IntervalSet::Minor;
    GF_EXPECT (intervalRatios (settings, 220, {}).values[1] == 1.2);
    settings.intervals = IntervalSet::Octaves;
    ratios = intervalRatios (settings, 220, {});
    GF_EXPECT (ratios.count == 3 && ratios.values[2] == 0.5);
}
GF_TEST (PitchSyncIntervals, scale_requires_a_known_key_and_uses_its_absolute_mask)
{
    Settings settings; settings.enabled = true; settings.voices = 4; settings.intervals = IntervalSet::Scale;
    const auto fallback = intervalRatios (settings, 220, {});
    GF_EXPECT (fallback.values == (std::array<double,4> { 1,1.5,2,3 }));
    const Scale cMajor { 0x0ab5, 0 };
    GF_EXPECT (absoluteScale (2, 0x0ab5).pitchClasses == 0x0ad6);
    GF_EXPECT (absoluteScale (-1, 0x0ab5).rootPc == -1 && absoluteScale (0, 0).rootPc == -1);
    const auto ratios = intervalRatios (settings, 220, cMajor);
    GF_EXPECT (ratios.count == 4 && ratios.values[0] == 1);
    GF_EXPECT (std::abs (ratios.values[1] - std::exp2 (2.0/12)) < 1e-12);
    GF_EXPECT (std::abs (ratios.values[2] - std::exp2 (3.0/12)) < 1e-12);
}
GF_TEST (PitchSyncIntervals, key_snap_is_cents_continuous_and_full_snap_is_in_key)
{
    Settings settings; settings.enabled = true; settings.voices = 4; settings.intervals = IntervalSet::Major;
    const Scale key { 0x0ab5, 0 };
    const auto just = intervalRatios (settings, 220, key);
    settings.snap01 = 1;
    const auto full = intervalRatios (settings, 220, key);
    settings.snap01 = 0.5;
    const auto half = intervalRatios (settings, 220, key);
    for (int i = 0; i < full.count; ++i)
    {
        const double note = 69 + 12 * std::log2 (220 * full.values[(std::size_t) i] / 440);
        const int nearest = (int) std::lround (note);
        GF_EXPECT (std::abs (note - nearest) < 1e-6 && (key.pitchClasses & (1u << ((nearest % 12 + 12) % 12))) != 0);
        GF_EXPECT (std::abs (half.values[(std::size_t) i] - std::sqrt (just.values[(std::size_t) i] * full.values[(std::size_t) i])) < 1e-12);
    }
}
GF_TEST (PitchSyncIntervals, bounds_and_private_seed_are_stable_without_scheduler_rng)
{
    Settings settings; settings.enabled = true; settings.voices = 99;
    for (int set = 0; set <= 5; ++set)
    {
        settings.intervals = (IntervalSet) set;
        const auto ratios = intervalRatios (settings, 1000, { 0x0fff, 0 });
        GF_EXPECT (ratios.count >= 1 && ratios.count <= 4);
        for (int i = 0; i < ratios.count; ++i) GF_EXPECT (ratios.values[(std::size_t) i] >= 0.25 && ratios.values[(std::size_t) i] <= 4);
    }
    GF_EXPECT (intervalRatios (settings, std::numeric_limits<float>::quiet_NaN(), {}).count == 0);
    detail::Random first (34), same (34), other (35);
    bool differs = false;
    for (int i = 0; i < 128; ++i)
    {
        const auto value = first.next();
        GF_EXPECT (value == same.next());
        differs = differs || value != other.next();
    }
    GF_EXPECT (differs);
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
