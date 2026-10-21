#include "call.h"
#include "ParamBuilders.h"

namespace gapfill
{

// F-L10 Call and response (docs/dev/features/F-L10 section 4). Neutral = fl10_mode Off.
void addCallParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APC> (makeID ("fl10_mode", 2), "Call Mode", juce::StringArray { "Off", "Complement", "Echo" }, 0));
    layout.add (std::make_unique<APF> (makeID ("fl10_strength", 2), "Call Strength",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 60.0f, Attr().withLabel ("%")));
    layout.add (std::make_unique<APInt> (makeID ("fl10_calls", 2), "Call Length", 2, 4, 3));
    layout.add (std::make_unique<APInt> (makeID ("fl10_replies", 2), "Response Length", 1, 4, 2));
}

void addCallPerformanceIds (juce::StringArray& ids)
{
    for (const char* id : { "fl10_mode", "fl10_strength", "fl10_calls", "fl10_replies" })
        ids.add (id);
}

} // namespace gapfill
