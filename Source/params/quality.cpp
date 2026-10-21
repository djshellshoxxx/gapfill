#include "quality.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addQualityParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // F-D03: Full is bit-identical to the baseline; Auto steps between levels from the CPU estimate.
    layout.add (std::make_unique<APC> (makeID ("qualityMode", 2), "Quality",
                                       juce::StringArray { "Full", "Balanced", "Lite", "Auto" }, 0));
}

void addQualityPerformanceIds (juce::StringArray& ids)
{
    ids.add ("qualityMode");
}

} // namespace gapfill
