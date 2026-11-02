// Synthetic native smoke harness. Link with Probe.cpp, JUCE modules and the
// repository AllocGuard; no real host/device measurement is established here.
#include <juce_audio_utils/juce_audio_utils.h>
#include "../../Tests/support/AllocGuard.h"
#include <cstring>
#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
class Clock final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo result;
        result.setIsPlaying (true); result.setBpm (120);
        result.setPpqPosition ((double) samples / 24000.0); result.setTimeInSamples (samples);
        return result;
    }
    std::int64_t samples = 0;
};
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    Clock clock;
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    processor->setRateAndBufferSizeDetails (48000, 512);
    processor->prepareToPlay (48000, 512);
    processor->setPlayHead (&clock);
    juce::AudioBuffer<float> audio (2, 512);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 512; ++sample) audio.setSample (channel, sample, .25f);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
    processor->processBlock (audio, midi); clock.samples += 512;
    long allocations = -1;
    {
        gftest::AllocGuard guard;
        for (int callback = 0; callback < 500; ++callback)
        { processor->processBlock (audio, midi); clock.samples += 512; }
        allocations = guard.count();
    }
    bool identical = processor->acceptsMidi() && processor->producesMidi() && midi.getNumEvents() == 1;
    const unsigned char expectedMidi[] { 0x90, 60, 127 };
    for (const auto event : midi)
        identical = identical && event.samplePosition == 0 && event.numBytes == 3
            && std::memcmp (event.data, expectedMidi, sizeof (expectedMidi)) == 0;
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 512; ++sample)
            identical = identical && audio.getSample (channel, sample) == .25f;
    processor->setPlayHead (nullptr);
    processor.reset();                            // finishes/drains/writes final metadata off the callback
    std::cout << "synthetic native callbacks=501; allocations=" << allocations << "; audio/MIDI identity=" << identical << '\n';
    return allocations == 0 && identical ? 0 : 1;
}
