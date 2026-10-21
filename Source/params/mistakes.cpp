#include "mistakes.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addMistakesParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // C-3: 0 = no mistakes, no roll, no RNG draw. At 100 % about one fill in five carries one.
    layout.add (percent ("mistakes", "Mistakes", 0.0f, 2));
}

void addMistakesPerformanceIds (juce::StringArray& ids)
{
    ids.add ("mistakes");
}

} // namespace gapfill
