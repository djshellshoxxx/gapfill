#include "MidiScheduler.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace gapfill::midiout
{

int MidiScheduler::finish (juce::MidiBuffer& midi, bool clearInput) noexcept
{
    if (clearInput) midi.clear();
    std::array<MidiPacket, kSchedulerMaxEvents> messages {};
    const int count = render (messages);
    for (int i = 0; i < count; ++i)
    {
        const auto& message = messages[(size_t) i];
        midi.addEvent (juce::MidiMessage (message.status, message.data1, message.data2), message.offset);
    }
    return count;
}

}
