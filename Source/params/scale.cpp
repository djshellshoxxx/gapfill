#include "scale.h"
#include "ParamBuilders.h"

namespace gapfill
{

// N-MUS Scale Lock (docs/dev/12 section 5; ids and ranges: docs/dev/02 registry). Neutral = scaleType Off.
void addScaleParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APC> (makeID ("scaleRoot", 2), "Scale Root",
                                       juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    layout.add (std::make_unique<APC> (makeID ("scaleType", 2), "Scale",
                                       juce::StringArray { "Off", "Major", "Natural Minor", "Dorian", "Phrygian", "Lydian",
                                                           "Mixolydian", "Harmonic Minor", "Melodic Minor", "Major Pentatonic",
                                                           "Minor Pentatonic", "Blues", "Whole Tone" }, 0));
    layout.add (std::make_unique<APF> (makeID ("scaleAmount", 2), "Scale Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
                                       Attr().withLabel ("%")));
}

void addScalePerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
