#include "scdrive.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addScdriveParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // F-L13: sidechain-driven fills. Inert without a connected sidechain bus.
    layout.add (percent ("scSnap", "Sidechain Snap", 0.0f, 2));
    layout.add (std::make_unique<APC> (makeID ("scGate", 2), "Sidechain Gate",
                                       juce::StringArray { "Off", "Fill when SC quiet", "Fill when SC active" }, 0));
    layout.add (std::make_unique<APF> (makeID ("scThresh", 2), "Sidechain Threshold",
                                       juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -30.0f,
                                       Attr().withLabel ("dB")
                                             .withStringFromValueFunction ([] (float v, int)
                                                 { return juce::String (v, 1) + " dB"; })));
}

void addScdrivePerformanceIds (juce::StringArray& ids)
{
    ids.add ("scSnap");
    ids.add ("scGate");
    ids.add ("scThresh");
}

} // namespace gapfill
