#include "keydet.h"
#include "ParamBuilders.h"

namespace gapfill
{

// F-X03 Automatic key detection. `scaleFollow` is the doc 12 registry ID of "Auto key"
// (docs/dev/features/F-X03 calls it fx03_auto); the other three are appended per
// docs/dev/features/DECISIONS.md item 1. Neutral = off.
void addKeydetParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APB> (makeID ("scaleFollow", 2), "Auto Key", false));
    layout.add (std::make_unique<APF> (makeID ("fx03_window", 2), "Key Window",
                                       juce::NormalisableRange<float> (4.0f, 30.0f, 0.1f), 12.0f,
                                       Attr().withLabel ("s")));
    layout.add (std::make_unique<APF> (makeID ("fx03_minConf", 2), "Key Min Confidence",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 40.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APB> (makeID ("fx03_hold", 2), "Key Hold", false));
}

void addKeydetPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
