#include "energy.h"
#include "ParamBuilders.h"

namespace gapfill
{

// F-L22 Musical energy tracking (docs/dev/12 registry). Neutral = energyAdapt 0.
void addEnergyParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APF> (makeID ("energyAdapt", 2), "Energy Follow",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APC> (makeID ("energyTarget", 2), "Energy Target",
                                       juce::StringArray { "Density", "Intensity", "Both" }, 2));
}

void addEnergyPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
