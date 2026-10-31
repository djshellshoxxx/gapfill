#include "../../PluginProcessor.h"

namespace gapfill
{
scenes::TransportSnap GapFillAudioProcessor::sceneTransportAt (int startSample, int n) const noexcept
{
    scenes::TransportSnap t;
    t.valid = hostTiming.playing && hostTiming.ppqValid && hostTiming.tempoValid;
    t.samplesPerBeat = currentSampleRate * 60.0 / hostTiming.bpm;
    t.ppq = hostTiming.ppq + (double) startSample / t.samplesPerBeat;
    t.sampleRate = currentSampleRate;
    t.tsNum = hostTiming.tsNum;
    t.tsDen = hostTiming.tsDen;
    t.numSamples = n;
    constexpr int phraseBars[] { 0, 2, 4, 8, 16 };
    t.phraseBars = phraseBars[juce::jlimit (0, 4, pi (params.phraseBars))];
    t.phraseOffset = pi (params.phraseOffset);
    return t;
}

int GapFillAudioProcessor::sceneSegmentLength (int startSample, int n) noexcept
{
    const auto prefix = liveControl.stageSceneChunk (sceneTransportAt (startSample, n));
    return prefix > 0 && prefix < n ? prefix : n;
}
} // namespace gapfill
