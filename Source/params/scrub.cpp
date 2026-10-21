#include "scrub.h"
#include "ParamBuilders.h"

namespace gapfill
{

// F-M19 Freeze & Scrub (docs/dev/12 section 7). Neutral = scrubOn off.
void addScrubParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APB> (makeID ("scrubOn", 2), "Scrub", false));
    layout.add (std::make_unique<APF> (makeID ("scrubPos", 2), "Scrub Position",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("scrubWidth", 2), "Scrub Width",
                                       juce::NormalisableRange<float> (10.0f, 500.0f, 0.1f), 80.0f,
                                       Attr().withLabel ("ms")));
}

void addScrubPerformanceIds (juce::StringArray& ids)
{
    // Performance state like Capture: Random / Surprise / Undo never touch it.
    ids.add ("scrubOn");
    ids.add ("scrubPos");
}

} // namespace gapfill
