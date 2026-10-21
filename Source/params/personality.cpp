#include "personality.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addPersonalityParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // C-1: Off = no overlay (identity).
    layout.add (std::make_unique<APC> (makeID ("personality", 2), "Personality",
                                       juce::StringArray { "Off", "Subtle", "Percussive", "Glitchy", "Psychedelic",
                                                           "Dub", "Aggressive", "Ambient", "Unhinged" }, 0));
}

void addPersonalityPerformanceIds (juce::StringArray& ids)
{
    ids.add ("personality");
}

} // namespace gapfill
