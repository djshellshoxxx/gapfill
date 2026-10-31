#include "../support/TestRig.h"

using namespace gapfill;
using namespace gftest;

GF_TEST (TimelineProducer, untransported_prepare_and_rate_changes_stamp_both_frame_generations)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "lookahead", 0.0f } });
    proc.getVisualisationFifo().setEnabled (true);
    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    for (double rate : { 96000.0, 48000.0 })
    {
        proc.setRateAndBufferSizeDetails (rate, 256);
        proc.prepareToPlay (rate, 256);
        audio.clear();
        proc.processBlock (audio, midi);
    }
    VizFrame frames[32];
    const auto count = proc.getVisualisationFifo().pop (frames, 32);
    GF_EXPECT (count >= 6);
    const auto firstEpoch = frames[0].timelineEpoch;
    GF_EXPECT (firstEpoch != 0 && frames[0].sampleRateHz == 96000);
    GF_EXPECT (frames[0].periodSamples == 128 && frames[0].posLo == 0);
    GF_EXPECT ((frames[0].flags & VizFrame::flagDiscontinuity) != 0);
    bool haveNew = false;
    for (int i = 0; i < count; ++i)
    {
        GF_EXPECT (frames[i].ppqTick == INT32_MIN);
        if (frames[i].timelineEpoch != firstEpoch)
        {
            if (! haveNew)
                GF_EXPECT (frames[i].posLo == 0 && (frames[i].flags & VizFrame::flagDiscontinuity) != 0);
            haveNew = true;
            GF_EXPECT (frames[i].sampleRateHz == 48000 && frames[i].periodSamples == 64);
        }
    }
    GF_EXPECT (haveNew);
}
