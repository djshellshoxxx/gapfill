#include "wetbusext.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addWetbusextParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    layout.add (std::make_unique<APC> (makeID ("wetBusMainMode", 2), "Main output mode",
                                      juce::StringArray { "Dry + Wet", "Dry only" }, 0));
    layout.add (std::make_unique<APF> (makeID ("wetBusLevel", 2), "Wet bus level",
                                      juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                      Attr().withLabel ("dB")
                                            .withStringFromValueFunction ([] (float value, int)
                                                { return juce::String (value, 1) + " dB"; })));
}

void addWetbusextPerformanceIds (juce::StringArray& ids)
{
    // Project routing state is excluded from Random, Undo and preset changes.
    ids.add ("wetBusMainMode");
    ids.add ("wetBusLevel");
}

} // namespace gapfill
