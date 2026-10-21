#include "fm10.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addFm10Parameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APB> (makeID ("fm10_stutter", 2), "Hold Stutter", false));
    layout.add (std::make_unique<APB> (makeID ("fm10_freeze", 2), "Hold Freeze", false));
    layout.add (std::make_unique<APB> (makeID ("fm10_reverse", 2), "Hold Reverse", false));
    layout.add (std::make_unique<APB> (makeID ("fm10_tape", 2), "Hold Tape Stop", false));
    layout.add (std::make_unique<APC> (makeID ("fm10_mode", 2), "Hold Mode", juce::StringArray { "Momentary", "Latch" }, 0));
    layout.add (percent ("fm10_level", "Hold Level", 100.0f));
    layout.add (percent ("fm10_dryDuck", "Hold Dry Duck", 0.0f));
    // Decision 7 (docs/dev/features/DECISIONS.md): "Respect protection" defaults to ON.
    layout.add (std::make_unique<APB> (makeID ("fm10_protect", 2), "Hold Respects Protection", true));
    layout.add (std::make_unique<APInt> (makeID ("fm10_timeout", 2), "Hold Timeout", 2, 60, 15));
}

void addFm10PerformanceIds (juce::StringArray& ids)
{
    for (const char* id : { "fm10_stutter", "fm10_freeze", "fm10_reverse", "fm10_tape", "fm10_mode", "fm10_level",
                            "fm10_dryDuck", "fm10_protect", "fm10_timeout" })
        ids.add (id);
}

} // namespace gapfill
