#include "midiout.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addMidioutParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // F-L11: MIDI out from gap / fill events. Routing state, not a sound: never randomised.
    layout.add (std::make_unique<APC> (makeID ("midiOutMode", 2), "MIDI Out Mode",
                                       juce::StringArray { "Off", "Gaps", "Fills", "Both" }, 0));
    layout.add (std::make_unique<APInt> (makeID ("midiOutNote", 2), "MIDI Out Note", 0, 127, 36));
    layout.add (std::make_unique<APInt> (makeID ("midiOutChan", 2), "MIDI Out Channel", 1, 16, 10));
}

void addMidioutPerformanceIds (juce::StringArray& ids)
{
    ids.add ("midiOutMode");
    ids.add ("midiOutNote");
    ids.add ("midiOutChan");
}

} // namespace gapfill
