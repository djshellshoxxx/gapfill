#include "midiext.h"
#include "ParamBuilders.h"

namespace gapfill
{

namespace
{
std::unique_ptr<builders::APInt> eventNote (const char* id, const char* name, int initial)
{
    return std::make_unique<builders::APInt> (builders::makeID (id, 3), name, 0, 127, initial,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int note, int)
            { return juce::String (note) + " = " + juce::MidiMessage::getMidiNoteName (note, true, true, 3); })
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getIntValue(); }));
}
}

void addMidiextParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    constexpr int version = 3;
    layout.add (std::make_unique<APB> (makeID ("fl11_on", version), "Extended MIDI Output", false));
    layout.add (std::make_unique<APInt> (makeID ("fl11_chan", version), "MIDI Event Channel", 1, 16, 10));
    layout.add (std::make_unique<APB> (makeID ("fl11_hit_on", version), "MIDI Hit Events", true));
    layout.add (std::make_unique<APB> (makeID ("fl11_gap_on", version), "MIDI Gap Events", false));
    layout.add (std::make_unique<APB> (makeID ("fl11_fill_on", version), "MIDI Fill Events", false));
    layout.add (eventNote ("fl11_note_hit", "MIDI Hit Note", 36));
    layout.add (eventNote ("fl11_note_gap", "MIDI Gap Note", 37));
    layout.add (eventNote ("fl11_note_fill", "MIDI Fill Note", 38));
    layout.add (std::make_unique<APC> (makeID ("fl11_vel", version), "MIDI Event Velocity",
                                    juce::StringArray { "Follow strength", "Fixed 100" }, 0));
    layout.add (millis ("fl11_len_ms", "MIDI Hit Length", 10.0f, 500.0f, 60.0f, 100.0f, version));
    layout.add (std::make_unique<APInt> (makeID ("fl11_cc", version), "MIDI Confidence CC", 0, 119, 0));
    layout.add (std::make_unique<APC> (makeID ("fl11_timing", version), "MIDI Hit Timing",
                                    juce::StringArray { "Output-aligned", "Detection-early" }, 0));
}

void addMidiextPerformanceIds (juce::StringArray& ids)
{
   #define MIDI_EXT_ID(member, id) ids.add (id);
    GAPFILL_PARAMS_MIDIEXT (MIDI_EXT_ID)
   #undef MIDI_EXT_ID
}

}
