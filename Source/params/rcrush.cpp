#include "rcrush.h"
#include "ParamBuilders.h"
#include "../Parameters.h"

namespace gapfill
{

// F-M14 Rhythmic bit/rate crush (docs/dev/12 section 7). Neutral = rcAmount 0.
void addRcrushParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APF> (makeID ("rcAmount", 2), "Crush Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APInt> (makeID ("rcBits", 2), "Crush Bits", 1, 16, 8));
    layout.add (std::make_unique<APInt> (makeID ("rcRate", 2), "Crush Rate", 1, 64, 4));

    juce::StringArray holds { "Free" };
    holds.addArray (divisionNames());
    layout.add (std::make_unique<APC> (makeID ("rcHold", 2), "Crush Hold", holds, 0));

    layout.add (std::make_unique<APF> (makeID ("rcTone", 2), "Crush Tone",
                                       juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                       Attr().withLabel ("%")));
}

void addRcrushPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
