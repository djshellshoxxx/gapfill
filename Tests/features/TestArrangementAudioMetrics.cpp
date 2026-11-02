// Actual source audio -> F-L22 frame features -> F-X04v2 metrics, without a DAW.
#if defined (GF_CONTEXT_STANDALONE)
 #include "ContextStandalone.h"
 #include <iostream>
#else
 #include "../TestFramework.h"
#endif
#include "../../Source/features/arrangement/SectionTracker.h"
#include "../../Source/features/context/EnergyTracker.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double kSr = 48000;
    constexpr int kFrame = 24000;
    using Signal = std::vector<float>;
    std::array<Signal, 2> loops()
    {
        std::array<Signal, 2> result;
        for (int section = 0; section < 2; ++section)
        {
            auto& signal = result[(std::size_t) section];
            signal.resize (96000);
            std::uint32_t random = 71423u;
            float low = 0;
            for (int i = 0; i < 96000; ++i)
            {
                const double t = i / kSr;
                random = random * 1664525u + 1013904223u;
                const float noise = (float) (random >> 8) / 8388608.0f - 1.0f;
                low += 0.6f * (noise - low);
                const double hat = std::fmod (t, 0.125);
                double x = hat < 0.025 ? 0.12 * std::exp (-hat * 160) * (noise - low) : 0;
                const double kick = std::fmod (t, 0.5);
                x += 0.5 * std::exp (-kick * 18) * std::sin (6.2831853 * (50 + 80 * std::exp (-kick * 40)) * kick);
                if (section == 1)
                {
                    const double snare = std::fmod (t + 0.5, 1.0);
                    x += 0.3 * noise * std::exp (-snare * 20) + 0.25 * std::sin (6.2831853 * 55 * t);
                    x += 0.15 * (std::sin (6.2831853 * 220 * t) + std::sin (6.2831853 * 277.2 * t) + std::sin (6.2831853 * 329.6 * t));
                }
                signal[(std::size_t) i] = (float) (x * (section == 0 ? 0.6 : 1.0));
            }
        }
        return result;
    }
    struct Runner
    {
        gapfill::context::EnergyTracker energy;
        gapfill::arrangement::SectionTracker tracker;
        Runner() { energy.prepare (kSr); tracker.setModelVersion (2); }
        void frame (const Signal& loop, int frameIndex, int block)
        {
            const auto* input = loop.data() + (frameIndex % 4) * kFrame;
            for (int pos = 0; pos < kFrame;)
            {
                const int n = std::min (block, kFrame - pos);
                energy.process (input + pos, n);
                pos += n;
            }
            tracker.pushFrame (energy.lastFrame(), 0.5f);
        }
    };
}

GF_TEST (ArrangementAudioMetrics, recurring_source_audio_forecasts_eighty_percent_within_one_frame)
{
    const auto signals = loops();
    for (int period : { 24, 32, 48, 64 })
    {
        Runner runner;
        int correct = 0, checked = 0;
        for (int frame = 0; frame < period * 8; ++frame)
        {
            runner.frame (signals[(std::size_t) (frame / period % 2)], frame % period, 512);
            if (frame >= period * 2 + 8)
            {
                ++checked;
                const int truth = (period - (frame + 1) % period) % period;
                const int forecast = runner.tracker.state().expectedIn;
                correct += forecast >= 0 && std::abs (forecast - truth) <= 1;
            }
        }
        std::printf ("  actual audio, period%d: forecasts%d/%d, boundaries%d\n", period, correct, checked, runner.tracker.boundaries());
        GF_EXPECT (checked > 100 && correct * 5 >= checked * 4);
    }
}

GF_TEST (ArrangementAudioMetrics, v2_source_frames_and_forecasts_are_chunk_invariant)
{
    const auto signals = loops();
    Runner fine, coarse;
    for (int frame = 0; frame < 256; ++frame)
    {
        const auto& signal = signals[(std::size_t) (frame / 32 % 2)];
        fine.frame (signal, frame % 32, 64);
        coarse.frame (signal, frame % 32, 4096);
        const auto& a = fine.tracker.state();
        const auto& b = coarse.tracker.state();
        GF_EXPECT (a.sectionIndex == b.sectionIndex && a.framesInto == b.framesInto && a.expectedIn == b.expectedIn);
        GF_EXPECT (a.level01 == b.level01 && a.energy01 == b.energy01 && a.trend == b.trend && a.confidence == b.confidence);
        GF_EXPECT (fine.tracker.lastBoundaryFrame() == coarse.tracker.lastBoundaryFrame());
    }
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
