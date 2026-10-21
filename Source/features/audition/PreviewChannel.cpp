#include "PreviewChannel.h"

namespace gapfill
{

const char* previewRejectionText (PreviewStatus s) noexcept
{
    switch (s)
    {
        case PreviewStatus::RejectedNoSource:   return "Nothing to sample - play some audio";
        case PreviewStatus::RejectedNoBand:     return "Enable a band to audition";
        case PreviewStatus::RejectedBuild:      return "Couldn't build a fill - try again";
        case PreviewStatus::RejectedBypassed:   return "Bypassed";
        case PreviewStatus::Idle:
        case PreviewStatus::Playing:
        default:                                return nullptr;
    }
}

void PreviewChannel::prepare (double sampleRate, int numChannels)
{
    sr = juce::jmax (8000.0, sampleRate);
    voice.prepare (sr, numChannels);
    tailLength = (int) std::ceil (sr * kPreviewRestartFade) + 4;
    tail.setSize (2, tailLength, false, true, false);
    scratch.setSize (2, 4096, false, true, false);
    reset();
}

void PreviewChannel::reset()
{
    voice.reset();
    tail.clear();
    tailPos = 0;
    tailActive = false;
    request = {};
    status = PreviewStatus::Idle;
    algo = -1;
    energy = 0.0;
    energySamples = 0;
    // `counter` and `seq` deliberately survive: only FillEngine::reset() restarts the sequence.
}

void PreviewChannel::fadeOut (int samples) noexcept
{
    if (voice.isActive())
        voice.requestStop (samples);
}

void PreviewChannel::beginRestart (const SourceHistory& history) noexcept
{
    if (! voice.isActive() || tailLength <= 0)
        return;

    tail.clear();
    voice.requestStop (tailLength - 2);
    voice.render (tail, 0, tailLength, history);
    tailPos = 0;
    tailActive = true;
}

void PreviewChannel::render (juce::AudioBuffer<float>& wet, int startSample, int numSamples,
                             const SourceHistory& history) noexcept
{
    if (tailActive)
    {
        const auto n = juce::jmin (numSamples, tailLength - tailPos);
        const auto chans = juce::jmin (wet.getNumChannels(), tail.getNumChannels());

        for (int ch = 0; ch < chans; ++ch)
            juce::FloatVectorOperations::add (wet.getWritePointer (ch, startSample),
                                              tail.getReadPointer (ch, tailPos), n);

        tailPos += n;

        if (tailPos >= tailLength)
            tailActive = false;
    }

    // Render through a scratch buffer: adding it to the wet bus is the same float addition the
    // voice would have made in place, and it lets us measure the preview's own level.
    int done = 0;

    while (voice.isActive() && done < numSamples)
    {
        const auto m = juce::jmin (numSamples - done, scratch.getNumSamples());
        scratch.clear (0, m);
        scratch.clear (1, m);
        voice.render (scratch, 0, m, history);

        const auto chans = juce::jmin (wet.getNumChannels(), 2);

        for (int ch = 0; ch < chans; ++ch)
        {
            const auto* src = scratch.getReadPointer (ch);
            auto* dst = wet.getWritePointer (ch, startSample + done);

            for (int i = 0; i < m; ++i)
            {
                dst[i] += src[i];
                energy += (double) src[i] * (double) src[i];
            }
        }

        energySamples += m;
        done += m;
    }
}

} // namespace gapfill
