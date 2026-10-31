#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "features/cloud/CloudMode.h"

#include <cmath>
#include <cstdio>

using namespace gapfill;

namespace
{
    struct Source
    {
        SourceHistory history;
        Source()
        {
            history.prepare (2, 48000 * 6);
            juce::AudioBuffer<float> source (2, 48000 * 3);
            juce::Random random (7);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < source.getNumSamples(); ++i)
                    source.setSample (ch, i, (random.nextFloat() * 2.0f - 1.0f) * 0.5f);
            history.write (source, 0, source.getNumSamples());
        }
    };

    double modulation (const SourceHistory& history, std::uint8_t version, std::uint64_t seed)
    {
        fx::CloudSettings settings; // canonical registry defaults: 20/s, 60 ms
        settings.modelVersion = version;
        fx::CloudMode mode;
        mode.configure (true, settings, 0.0f, history.getWritePosition() - 1, seed, 48000.0);
        double envelopes[70] {};
        int count = 0;
        double energy = 0.0;
        for (int i = 0; i < 48000; ++i)
        {
            float l = 0.0f, r = 0.0f;
            mode.generate (l, r, history, 2);
            energy += (double) l * l + (double) r * r;
            if ((i + 1) % 480 == 0)
            {
                if (i > 7200 && i < 40800)
                    envelopes[count++] = std::sqrt (energy / 480.0);
                energy = 0.0;
            }
        }
        double mean = 0.0, variance = 0.0;
        for (int i = 0; i < count; ++i) mean += envelopes[i];
        mean /= count;
        for (int i = 0; i < count; ++i) variance += (envelopes[i] - mean) * (envelopes[i] - mean);
        return std::sqrt (variance / count) / std::max (1.0e-12, mean);
    }
}

GF_TEST (CloudModel, default_parameter_values_reduce_window_modulation_across_seeds)
{
    Source source;
    double legacy = 0.0, latest = 0.0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        legacy += modulation (source.history, 1, seed);
        latest += modulation (source.history, 2, seed);
    }
    std::printf ("         Cloud default white-noise-source CV: legacy %.6f, v2 %.6f (20 seeds)\n", legacy / 20.0, latest / 20.0);
    GF_EXPECT (latest <= legacy * 0.8);
}

GF_TEST (CloudModel, both_versions_are_bounded_deterministic_and_allocation_free)
{
    Source source;
    for (const auto version : { std::uint8_t { 1 }, std::uint8_t { 2 } })
    {
        fx::CloudSettings settings;
        settings.modelVersion = version;
        settings.densityHz = 80.0f;
        settings.sizeMs = 250.0f;
        fx::CloudMode first, second;
        first.configure (true, settings, 0.5f, source.history.getWritePosition() - 1, 456, 48000.0);
        second.configure (true, settings, 0.5f, source.history.getWritePosition() - 1, 456, 48000.0);
        gftest::AllocGuard guard;
        for (int i = 0; i < 24000; ++i)
        {
            float l = 0.0f, r = 0.0f, a = 0.0f, b = 0.0f;
            GF_EXPECT (first.generate (l, r, source.history, 2));
            second.generate (a, b, source.history, 2);
            GF_EXPECT (l == a && r == b && std::isfinite (l) && std::isfinite (r));
            GF_EXPECT (first.activeGrains() <= fx::kCloudMaxGrains);
        }
        GF_EXPECT (guard.count() == 0);
    }
}
