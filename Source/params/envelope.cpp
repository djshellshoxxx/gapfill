#include "envelope.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addEnvelopeParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // C-4: Flat / amount 0 are neutral (the envelope stage is skipped entirely).
    layout.add (std::make_unique<APC> (makeID ("fillShape", 2), "Fill Shape",
                                       juce::StringArray { "Flat", "Rise", "Fall", "Rise-Fall", "Random Walk" }, 0));
    layout.add (percent ("fillShapeAmount", "Fill Shape Amount", 0.0f, 2));
}

void addEnvelopePerformanceIds (juce::StringArray& ids)
{
    ids.add ("fillShape");
    ids.add ("fillShapeAmount");
}

} // namespace gapfill
