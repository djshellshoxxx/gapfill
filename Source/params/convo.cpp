#include "convo.h"
#include "ParamBuilders.h"
#include "../Parameters.h"

namespace gapfill
{

// F-L21 Transient-derived convolution (docs/dev/12 section 7). Neutral = cvAmount 0.
void addConvoParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APF> (makeID ("cvAmount", 2), "Convolution Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("cvSize", 2), "Convolution Size",
                                       juce::NormalisableRange<float> (20.0f, 500.0f, 0.1f), 120.0f,
                                       Attr().withLabel ("ms")));
    layout.add (std::make_unique<APF> (makeID ("cvDamp", 2), "Convolution Damping",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 40.0f,
                                       Attr().withLabel ("%")));
}

void addConvoPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
