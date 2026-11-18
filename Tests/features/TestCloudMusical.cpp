#include "../support/TestRig.h"
#include "../support/VoiceRig.h"

#include <array>
#include <cmath>
#include <cstdio>

using namespace gapfill;
using namespace gftest;

namespace
{
    double rmsModulation (const juce::AudioBuffer<float>& audio)
    {
        std::array<double, 70> envelope {};
        std::size_t n = 0;
        // Same 10ms stereo RMS metric and edge exclusion as TestCloud.
        for (int sample = 7200; sample + 480 < 40800; sample += 480)
            envelope[n++] = std::hypot (rmsOf (audio, sample, 480, 0), rmsOf (audio, sample, 480, 1));
        double mean = 0.0, variance = 0.0;
        for (std::size_t i = 0; i < n; ++i) mean += envelope[i];
        mean /= (double) n;
        for (std::size_t i = 0; i < n; ++i) variance += (envelope[i] - mean) * (envelope[i] - mean);
        return std::sqrt (variance / (double) n) / std::max (1.0e-12, mean);
    }
}

GF_TEST (CloudMusical, drum_loop_at_registry_defaults_reduces_modulation_by_twenty_percent)
{
    VoiceRig rig;
    const auto source = drumLoop (48000.0, 150.0, 3);
    rig.history.clear();
    rig.history.write (source, 0, source.getNumSamples());
    double plainSum = 0.0, cloudSum = 0.0;
    constexpr int seeds = 20, anchors = 4;
    for (int anchor = 0; anchor < anchors; ++anchor)
    {
        rig.anchor = source.getNumSamples() - 1 - anchor * 4800;
        for (std::uint64_t seed = 1; seed <= seeds; ++seed)
            for (const auto useCloud : { false, true })
            {
                FillVoice voice;
                voice.prepare (48000.0, 2);
                auto setup = rig.setup (Algo::Granular, 1000.0, seed);
                // Identical registry macro defaults for both renders. Only
                // Cloud's enabled render mode differs; all Cloud controls
                // retain their actual defaults (20/s,60ms,600ms,40%,100c,60%).
                setup.intensity01 = 0.5f;
                setup.complexity01 = 0.4f;
                setup.chaos01 = 0.25f;
                setup.modes.cloud = useCloud;
                setup.fxStart.settings.cloud.amount01 = useCloud ? 1.0f : 0.0f;
                setup.fxStart.settings.cloud.modelVersion = 2;
                const auto rendered = rig.render (voice, setup, setup.lengthSamples, 64);
                GF_EXPECT (allFinite (rendered));
                const auto depth = rmsModulation (rendered);
                if (useCloud) cloudSum += depth;
                else plainSum += depth;
            }
    }
    std::printf ("         Drum Cloud defaults: %d seeds x%d anchors, plainCV %.6f, cloudCV %.6f, reduction %.2f%%\n",
                 seeds, anchors, plainSum / (seeds * anchors), cloudSum / (seeds * anchors),
                 100.0 * (1.0 - cloudSum / plainSum));
    GF_EXPECT (cloudSum <= plainSum * 0.8);
}
