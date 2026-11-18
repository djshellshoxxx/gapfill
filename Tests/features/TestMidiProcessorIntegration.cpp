#include "../support/TestRig.h"
#include "../support/LiveRig.h"
#include "../support/AllocGuard.h"
#include <vector>

using namespace gapfill;
using namespace gftest;

namespace
{
    struct MidiRun { std::vector<std::int64_t> hits; int frames = 0; };
    MidiRun hitTimes (int blockSize, bool early)
    {
        constexpr double rate = 48000.0;
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 0 }, { "lookahead", 100 }, { "fl11_on", 1 },
                          { "fl11_hit_on", 1 }, { "fl11_timing", early ? 1.0f : 0.0f }, { "fl11_chan", 11 } });
        prepareProcessor (proc, rate, blockSize, false);
        const auto input = drumLoop (rate, 120.0, 2);
        juce::AudioBuffer<float> audio (2, blockSize);
        juce::MidiBuffer midi;
        midi.ensureSize (2048);
        MidiRun result;
        for (int start = 0; start + blockSize <= input.getNumSamples(); start += blockSize)
        {
            for (int channel = 0; channel < 2; ++channel)
                audio.copyFrom (channel, 0, input, channel, start, blockSize);
            midi.clear();
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 100), 0);
            {
                AllocGuard allocations;
                proc.processBlock (audio, midi);
                GF_EXPECT (allocations.count() == 0);
            }
            GF_EXPECT (midi.getNumEvents() <= 32);
            for (const auto event : midi)
            {
                const auto message = event.getMessage();
                GF_EXPECT (! message.isController());
                GF_EXPECT (event.samplePosition >= 0 && event.samplePosition < blockSize);
                if (message.isNoteOn())
                {
                    GF_EXPECT (message.getChannel() == 11 && message.getNoteNumber() == 36);
                    result.hits.push_back ((std::int64_t) start + event.samplePosition);
                }
            }
            result.frames += blockSize;
        }
        GF_EXPECT (! result.hits.empty());
        return result;
    }
}

GF_TEST (MidiProcessor, detection_early_hits_are_causal_and_independent_of_host_callback_size)
{
    const auto reference = hitTimes (32, true);
    for (int block : { 64, 512, 8192 })
    {
        const auto actual = hitTimes (block, true);
        // The large-block render ends earlier; compare their common duration.
        std::vector<std::int64_t> expected;
        for (auto time : reference.hits)
            if (time < actual.frames)
                expected.push_back (time);
        GF_EXPECT (actual.hits == expected);
    }
}

GF_TEST (MidiProcessor, extended_output_uses_engine_events_with_no_input_echo)
{
    GF_EXPECT (! hitTimes (512, false).hits.empty());
}

GF_TEST (MidiProcessor, declared_output_never_echoes_input_when_runtime_masters_are_off)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 64, false);
    juce::AudioBuffer<float> audio (2, 64);
    audio.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 100), 12);
    proc.processBlock (audio, midi);
    GF_EXPECT (midi.getNumEvents() == (midiout::kDeclared ? 0 : 1));
}
