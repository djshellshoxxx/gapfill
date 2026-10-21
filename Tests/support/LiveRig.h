#pragma once

// Offline render helper for the WP6 live-control tests: like gftest::renderOffline
// but the per-block callback also receives the MidiBuffer, and the block's
// first sample / ppq are exposed.

#include "TestRig.h"

#include <functional>

namespace gftest
{

struct LiveRender
{
    juce::AudioBuffer<float> out;
    int fills = 0;
};

inline LiveRender renderLive (gapfill::GapFillAudioProcessor& proc,
                              const juce::AudioBuffer<float>& in,
                              int blockSize, double sampleRate,
                              TestPlayHead& playHead,
                              const std::function<void (int blockIndex, int startSample, juce::MidiBuffer&)>& onBlock,
                              bool prepare = true)
{
    if (prepare)
        prepareProcessor (proc, sampleRate, blockSize, false);

    proc.setPlayHead (&playHead);

    LiveRender result;
    result.out.setSize (2, in.getNumSamples());
    result.out.clear();

    juce::AudioBuffer<float> work (2, blockSize);
    juce::MidiBuffer midi;
    int blockIndex = 0;

    for (int pos = 0; pos < in.getNumSamples(); pos += blockSize, ++blockIndex)
    {
        const auto n = juce::jmin (blockSize, in.getNumSamples() - pos);
        midi.clear();

        if (onBlock)
            onBlock (blockIndex, pos, midi);

        work.clear();

        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, in, juce::jmin (ch, in.getNumChannels() - 1), pos, n);

        juce::AudioBuffer<float> slice (work.getArrayOfWritePointers(), 2, n);
        proc.processBlock (slice, midi);

        for (int ch = 0; ch < 2; ++ch)
            result.out.copyFrom (ch, pos, work, ch, 0, n);

        playHead.advance (n, sampleRate);
    }

    proc.setPlayHead (nullptr);
    result.fills = proc.getFillEngine().getFillsStarted();
    return result;
}

} // namespace gftest
