#include "evolve.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addEvolveParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // C-8: Off = neutral. The other choices are the length of one waypoint cycle in bars.
    layout.add (std::make_unique<APC> (makeID ("evolve", 2), "Evolve",
                                       juce::StringArray { "Off", "8 bars", "16 bars", "32 bars" }, 0));
}

void addEvolvePerformanceIds (juce::StringArray& ids)
{
    ids.add ("evolve");
}

} // namespace gapfill
