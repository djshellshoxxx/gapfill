#include "coop.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addCoopParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // F-X06: cooperation between GapFill instances in one process. Off = nothing is shared.
    layout.add (std::make_unique<APC> (makeID ("coopGroup", 2), "Co-op Group",
                                       juce::StringArray { "Off", "A", "B", "C", "D" }, 0));
    layout.add (std::make_unique<APC> (makeID ("coopMode", 2), "Co-op Mode",
                                       juce::StringArray { "Avoid overlap", "Alternate", "Follow" }, 0));
}

void addCoopPerformanceIds (juce::StringArray& ids)
{
    ids.add ("coopGroup");
    ids.add ("coopMode");
}

} // namespace gapfill
