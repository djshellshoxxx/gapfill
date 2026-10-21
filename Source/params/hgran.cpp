#include "hgran.h"
#include "ParamBuilders.h"

namespace gapfill
{

// F-X01 Harmonic granular resynthesis (docs/dev/12 registry). Neutral = hgAmount 0.
void addHgranParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APF> (makeID ("hgAmount", 2), "Harmonic Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f, Attr().withLabel ("%")));
    layout.add (std::make_unique<APInt> (makeID ("hgVoices", 2), "Harmonic Voices", 1, 6, 3));
    layout.add (std::make_unique<APF> (makeID ("hgMotion", 2), "Harmonic Motion",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 30.0f, Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("hgTone", 2), "Harmonic Tone",
                                       juce::NormalisableRange<float> (-100.0f, 100.0f, 0.01f), 0.0f, Attr().withLabel ("%")));
}

void addHgranPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
