#include "resonator.h"
#include "ParamBuilders.h"
#include "../Parameters.h"

namespace gapfill
{

// F-M15 Comb resonator (docs/dev/12 section 7). Neutral = resAmount 0.
void addResonatorParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APF> (makeID ("resAmount", 2), "Resonator Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APInt> (makeID ("resNote", 2), "Resonator Note", 24, 96, 60));
    layout.add (std::make_unique<APC> (makeID ("resMode", 2), "Resonator Mode",
                                       juce::StringArray { "Manual", "Scale degrees" }, 0));
    layout.add (std::make_unique<APF> (makeID ("resDecay", 2), "Resonator Decay",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("resDamp", 2), "Resonator Damping",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 40.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("resSpread", 2), "Resonator Spread",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 30.0f,
                                       Attr().withLabel ("%")));
}

void addResonatorPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
