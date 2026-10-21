#include "bandfx.h"
#include "ParamBuilders.h"

namespace gapfill
{

// F-L14 Multiband effect routing (docs/dev/12 section 7). All on = neutral.
void addBandfxParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APB> (makeID ("bandLowFx", 2),  "Low Band Effects",  true));
    layout.add (std::make_unique<APB> (makeID ("bandMidFx", 2),  "Mid Band Effects",  true));
    layout.add (std::make_unique<APB> (makeID ("bandHighFx", 2), "High Band Effects", true));
}

void addBandfxPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
