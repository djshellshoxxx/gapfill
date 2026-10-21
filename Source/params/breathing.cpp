#include "breathing.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addBreathingParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // C-6: off / depth 0 are neutral.
    layout.add (std::make_unique<APB> (makeID ("densityBreath", 2), "Density Breathing", false));
    layout.add (std::make_unique<APC> (makeID ("densityBreathBars", 2), "Breathing Bars",
                                       juce::StringArray { "1", "2", "4", "8" }, 2));
    layout.add (percent ("densityBreathDepth", "Breathing Depth", 0.0f, 2));
    layout.add (std::make_unique<APC> (makeID ("densityBreathCurve", 2), "Breathing Curve",
                                       juce::StringArray { "Sine", "Rise-Fall", "Pulse" }, 0));
}

void addBreathingPerformanceIds (juce::StringArray& ids)
{
    for (const char* id : { "densityBreath", "densityBreathBars", "densityBreathDepth", "densityBreathCurve" })
        ids.add (id);
}

} // namespace gapfill
