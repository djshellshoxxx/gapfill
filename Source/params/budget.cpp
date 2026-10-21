#include "budget.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addBudgetParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // F-D01: both Off = neutral. Fills per bar (index -> 0,1,2,3,4,6,8) and a no-start zone.
    layout.add (std::make_unique<APC> (makeID ("fillBudget", 2), "Fills Per Bar",
                                       juce::StringArray { "Off", "1", "2", "3", "4", "6", "8" }, 0));
    layout.add (std::make_unique<APC> (makeID ("quietZone", 2), "Quiet Zone",
                                       juce::StringArray { "Off", "Beat 1", "Last beat", "Last 2 beats", "Last bar of phrase" }, 0));
}

void addBudgetPerformanceIds (juce::StringArray& ids)
{
    ids.add ("fillBudget");
    ids.add ("quietZone");
}

} // namespace gapfill
