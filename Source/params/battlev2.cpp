#include "battlev2.h"
#include "ParamBuilders.h"

namespace gapfill
{
void addBattlev2Parameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    layout.add (std::make_unique<APB> (makeID ("fm18_on", 2), "Detailed Battle", false));
    layout.add (std::make_unique<APC> (makeID ("fm18_unit", 2), "Battle window",
        juce::StringArray { "1 bar", "2 bars", "4 bars", "8 bars" }, 1));
    layout.add (std::make_unique<APC> (makeID ("fm18_first", 2), "First Battle style",
        juce::StringArray { "Style 1", "Style 2" }, 0));
}

void addBattlev2PerformanceIds (juce::StringArray& ids)
{
    ids.addArray ({ "fm18_on", "fm18_unit", "fm18_first" });
}
} // namespace gapfill
