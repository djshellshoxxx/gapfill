#include "wetout.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addWetoutParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // F-M17: routing switch. On = the main output carries only the wet bus at unity.
    layout.add (std::make_unique<APB> (makeID ("wetSolo", 2), "Wet Only Output", false));
}

void addWetoutPerformanceIds (juce::StringArray& ids)
{
    ids.add ("wetSolo");   // routing state: not randomised, not part of Undo snapshots, not stored in scenes
}

} // namespace gapfill
