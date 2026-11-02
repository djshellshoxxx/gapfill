#if defined (GF_CONTEXT_STANDALONE)
 #include "ContextStandalone.h"
 #include <iostream>
#else
 #include "../TestFramework.h"
 #include "../support/AllocGuard.h"
#endif
#include "../../Source/features/harmonic/psola/PitchMarks.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
using namespace gapfill::harmonic::psola;
namespace
{
    std::vector<float> signal (double sr, double hz, int count)
    {
        std::vector<float> x ((std::size_t) count);
        for (int i = 0; i < count; ++i)
            x[(std::size_t) i] = (float) (0.3 * std::sin (6.283185307179586 * hz * i / sr));
        return x;
    }
}
GF_TEST (PitchMarks, periods_are_found_with_complete_windows_at_four_rates)
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (float hz : { 55.0f, 220.0f, 1000.0f })
        {
            PitchMarks finder; finder.prepare (sr);
            const auto x = signal (sr, hz, (int) (sr * 0.32));
            const auto result = finder.find ({ x.data(), nullptr, (int) x.size(), 1 }, hz);
            GF_EXPECT (result.ready() && result.epochs.count <= kEpochs && result.epochs.spacingCv <= 0.15f);
            const double period = sr / hz;
            for (int i = 0; i < result.epochs.count; ++i)
            {
                const int at = result.epochs.positions[(std::size_t) i];
                GF_EXPECT (at >= std::ceil (period) && at + std::ceil (period) < (int) x.size());
                if (i > 0)
                    GF_EXPECT (std::abs (at - result.epochs.positions[(std::size_t) (i - 1)] - period) <= 2);
            }
            GF_EXPECT (finder.preallocatedBytes() <= (std::size_t) kMaxCaptureSamples * sizeof (float));
        }
}
GF_TEST (PitchMarks, short_invalid_and_nonfinite_capture_falls_back_safely)
{
    PitchMarks finder; finder.prepare (48000);
    auto x = signal (48000, 220, 512);
    GF_EXPECT (! finder.find ({ x.data(), nullptr, (int) x.size(), 1 }, 220).ready());
    GF_EXPECT (finder.find ({}, 220).failure == EpochFailure::InvalidCapture);
    GF_EXPECT (finder.find ({ x.data(), nullptr, (int) x.size(), 1 }, 0).failure == EpochFailure::InvalidPitch);
    x = signal (48000, 220, 15360);
    x[10] = std::numeric_limits<float>::infinity();
    GF_EXPECT (finder.find ({ x.data(), nullptr, (int) x.size(), 1 }, 220).failure == EpochFailure::NonFinite);
    x[10] = 0;
    GF_EXPECT (finder.find ({ x.data(), nullptr, (int) x.size(), 2 }, 220).failure == EpochFailure::InvalidCapture);
    std::fill (x.begin(), x.end(), 0);
    GF_EXPECT (! finder.find ({ x.data(), nullptr, (int) x.size(), 1 }, 220).ready());
}
GF_TEST (PitchMarks, analysis_preserves_capture_and_is_repeatable)
{
    PitchMarks finder; finder.prepare (48000);
    const auto x = signal (48000, 220, 15360);
    const auto original = x;
    const auto a = finder.find ({ x.data(), x.data(), (int) x.size(), 2 }, 220);
    const auto b = finder.find ({ x.data(), x.data(), (int) x.size(), 2 }, 220);
    GF_EXPECT (x == original && a.ready() && b.ready());
    GF_EXPECT (a.epochs.positions == b.epochs.positions && a.epochs.count == b.epochs.count);
}
GF_TEST (PitchMarks, unstable_period_spacing_is_an_explicit_fallback)
{
    PitchMarks finder; finder.prepare (48000);
    std::vector<float> x (15360, 0);
    int index = 1000, epoch = 0;
    while (index < (int) x.size())
    {
        x[(std::size_t) index] = 1;
        index += epoch++ % 2 == 0 ? 142 : 294;
    }
    const auto result = finder.find ({ x.data(), nullptr, (int) x.size(), 1 }, 220);
    GF_EXPECT (! result.ready() && result.failure == EpochFailure::Irregular && result.epochs.spacingCv > 0.15f);
}
#if ! defined (GF_CONTEXT_STANDALONE)
GF_TEST (PitchMarks, repeated_epoch_queries_do_not_allocate_on_audio)
{
    PitchMarks finder; finder.prepare (192000);
    const auto x = signal (192000, 55, 61440);
    long allocations = 0;
    bool ready = false;
    {
        gftest::AllocGuard guard;
        for (int i = 0; i < 4; ++i) ready = finder.find ({ x.data(), nullptr, (int) x.size(), 1 }, 55).ready();
        allocations = guard.count();
    }
    GF_EXPECT (ready && allocations == 0);
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
