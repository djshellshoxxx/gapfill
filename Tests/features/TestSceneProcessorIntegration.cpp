#include "../support/TestRig.h"
#include "features/scenes/SceneModel.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    juce::AudioBuffer<float> renderSceneBoundary (bool useScene)
    {
        constexpr double sr = 48000.0;
        constexpr int block = 8192, boundary = 96000;
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 100 }, { "density", 100 }, { "seed", 37 }, { "fsdAmount", 0 } });
        if (useScene)
        {
            setParams (proc, { { "fsdAmount", 70 } });
            GF_EXPECT (proc.getLiveControl().scenes().saveCurrent (0, "Boundary FX", scenes::ScopeFx));
            setParams (proc, { { "fsdAmount", 0 } });
            GF_EXPECT (proc.getLiveControl().scenes().gotoSceneAt (0, 4.0));
        }
        const auto input = drumLoop (sr, 120.0, 2);
        juce::AudioBuffer<float> result (2, input.getNumSamples());
        TestPlayHead playHead;
        playHead.bpm = 120.0;
        proc.setPlayHead (&playHead);
        proc.setRateAndBufferSizeDetails (sr, block);
        proc.prepareToPlay (sr, block);
        for (int pos = 0; pos < input.getNumSamples();)
        {
            int n = juce::jmin (block, input.getNumSamples() - pos);
            if (! useScene && pos < boundary)
                n = juce::jmin (n, boundary - pos);
            if (! useScene && pos == boundary)
                setParams (proc, { { "fsdAmount", 70 } });
            playHead.ppq = (double) pos / (sr * 0.5);
            juce::AudioBuffer<float> chunk (2, n);
            for (int channel = 0; channel < 2; ++channel)
                chunk.copyFrom (channel, 0, input, channel, pos, n);
            juce::MidiBuffer midi;
            proc.processBlock (chunk, midi);
            for (int channel = 0; channel < 2; ++channel)
                result.copyFrom (channel, pos, chunk, channel, 0, n);
            pos += n;
        }
        GF_EXPECT (proc.getFillEngine().getFillsStarted() > 0);
        proc.setPlayHead (nullptr);
        return result;
    }
}

GF_TEST (SceneProcessor, bus_effect_recall_matches_a_change_at_the_exact_boundary_sample)
{
    const auto expected = renderSceneBoundary (false);
    const auto actual = renderSceneBoundary (true);
    const auto difference = compareBuffers (expected, actual, 1.0e-6f);
    GF_EXPECT_MSG (difference.firstBad < 0, "Scene boundary mismatch at sample " + juce::String (difference.firstBad));
}
