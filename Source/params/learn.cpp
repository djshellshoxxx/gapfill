#include "learn.h"
#include "ParamBuilders.h"

namespace gapfill
{

// F-X05 Local preference learning (docs/dev/features/F-X05 section 4). Neutral = fx05_on off.
void addLearnParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APB> (makeID ("fx05_on", 2), "Learn My Taste", false));
    layout.add (std::make_unique<APF> (makeID ("fx05_strength", 2), "Taste Strength",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, Attr().withLabel ("%")));
}

void addLearnPerformanceIds (juce::StringArray& ids)
{
    ids.add ("fx05_on");
}

} // namespace gapfill
