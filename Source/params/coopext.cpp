#include "coopext.h"
#include "ParamBuilders.h"

namespace gapfill
{
void addCoopextParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    layout.add (std::make_unique<APB> (makeID ("coopDeterministic", 3), "Deterministic Co-op", false));
    layout.add (std::make_unique<APC> (makeID ("coopDetGroup", 3), "Deterministic Group",
        juce::StringArray { "Use legacy group", "A", "B", "C", "D", "E", "F", "G", "H" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<APInt> (makeID ("coopSlot", 3), "Co-op Fixed Slot", 1, 32, 1,
        juce::AudioParameterIntAttributes().withAutomatable (false)));
    layout.add (std::make_unique<APInt> (makeID ("coopPriority", 3), "Co-op Priority", 0, 100, 50,
        juce::AudioParameterIntAttributes().withLabel ("%")));
    layout.add (percent ("coopYield", "Co-op Yield", 70.0f, 3));
    layout.add (std::make_unique<APC> (makeID ("coopGrid", 3), "Co-op Grid",
        juce::StringArray { "1 beat", "2 beats", "1 bar", "2 bars", "4 bars" }, 2,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
}

void addCoopextPerformanceIds (juce::StringArray& ids)
{
   #define ADD_ID(member, id) ids.add (id);
    GAPFILL_PARAMS_COOPEXT (ADD_ID)
   #undef ADD_ID
}
}
