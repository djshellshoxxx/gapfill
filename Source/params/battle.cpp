#include "battle.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addBattleParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    layout.add (std::make_unique<APC> (makeID ("battleEvery", 2), "Beat Battle",
                                       juce::StringArray { "Off", "1 bar", "2 bars", "4 bars", "8 bars" }, 0));
}

void addBattlePerformanceIds (juce::StringArray& ids)
{
    ids.add ("battleEvery");     // performance state: not randomised, not in Undo snapshots
}

} // namespace gapfill
