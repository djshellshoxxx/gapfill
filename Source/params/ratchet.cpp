#include "ratchet.h"
#include "ParamBuilders.h"
#include "../Parameters.h"

namespace gapfill
{

// F-M16 Rhythmic ratchet (docs/dev/12 section 7). Neutral = ratAmount 0.
void addRatchetParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APF> (makeID ("ratAmount", 2), "Ratchet Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APC> (makeID ("ratDiv", 2), "Ratchet Division", divisionNames(),
                                       (int) Division::Sixteenth));
    layout.add (std::make_unique<APInt> (makeID ("ratRepeats", 2), "Ratchet Repeats", 2, 16, 4));
    layout.add (std::make_unique<APF> (makeID ("ratAccel", 2), "Ratchet Accel",
                                       juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("ratGate", 2), "Ratchet Gate",
                                       juce::NormalisableRange<float> (10.0f, 100.0f, 0.1f), 70.0f,
                                       Attr().withLabel ("%")));
}

void addRatchetPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
