#include "safety.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addSafetyParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    layout.add (std::make_unique<APB> (makeID ("wetSafety", 2), "Wet Safety", false));
}

void addSafetyPerformanceIds (juce::StringArray& ids)
{
    ids.add ("wetSafety");      // performance state: not randomised, not part of Undo snapshots
}

} // namespace gapfill
