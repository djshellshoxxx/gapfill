#include "spfreeze.h"
#include "ParamBuilders.h"
#include "../Parameters.h"

namespace gapfill
{

// F-X02 Spectral freeze and morph (docs/dev/12 section 7). Neutral = sfzAmount 0.
void addSpfreezeParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APF> (makeID ("sfzAmount", 2), "Freeze Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("sfzLength", 2), "Freeze Length",
                                       juce::NormalisableRange<float> (50.0f, 4000.0f, 0.1f), 800.0f,
                                       Attr().withLabel ("ms")));
    layout.add (std::make_unique<APF> (makeID ("sfzDecay", 2), "Freeze Decay",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("sfzSmear", 2), "Freeze Smear",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 30.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("sfzMorph", 2), "Freeze Morph",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("sfzPitch", 2), "Freeze Pitch",
                                       juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("st")));
    layout.add (std::make_unique<APF> (makeID ("sfzWidth", 2), "Freeze Width",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f,
                                       Attr().withLabel ("%")));
}

void addSpfreezePerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
