#include "swell.h"
#include "ParamBuilders.h"

namespace gapfill
{

// N-FX1 Reverse Swell (docs/dev/12 section 4). Neutral = swellAmount 0.
void addSwellParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APF> (makeID ("swellAmount", 2), "Swell Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APC> (makeID ("swellCurve", 2), "Swell Curve",
                                       juce::StringArray { "Linear", "Exponential", "S-curve", "Late" }, 1));
    layout.add (std::make_unique<APF> (makeID ("swellFilter", 2), "Swell Filter",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 60.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APC> (makeID ("swellTarget", 2), "Swell Target",
                                       juce::StringArray { "Gap end", "Next beat", "Next bar" }, 0));
}

void addSwellPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
