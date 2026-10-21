#include "dub.h"
#include "ParamBuilders.h"
#include "../Parameters.h"

namespace gapfill
{

// F-L15 Dynamic dub feedback (docs/dev/12 section 7). Neutral = dubAmount 0.
void addDubParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APF> (makeID ("dubAmount", 2), "Dub Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APC> (makeID ("dubDiv", 2), "Dub Division", divisionNames(), 3));
    layout.add (std::make_unique<APF> (makeID ("dubFeedback", 2), "Dub Feedback",
                                       juce::NormalisableRange<float> (0.0f, 90.0f, 0.01f), 50.0f,
                                       Attr().withLabel ("%")));
    {
        auto range = juce::NormalisableRange<float> (20.0f, 2000.0f, 0.1f);
        range.setSkewForCentre (300.0f);
        layout.add (std::make_unique<APF> (makeID ("dubLow", 2), "Dub Low Cut", range, 120.0f,
                                           Attr().withLabel ("Hz")));
    }
    {
        auto range = juce::NormalisableRange<float> (500.0f, 18000.0f, 0.1f);
        range.setSkewForCentre (3000.0f);
        layout.add (std::make_unique<APF> (makeID ("dubHigh", 2), "Dub High Cut", range, 4000.0f,
                                           Attr().withLabel ("Hz")));
    }
    layout.add (std::make_unique<APF> (makeID ("dubMove", 2), "Dub Move",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 30.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("dubSat", 2), "Dub Saturation",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 20.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APB> (makeID ("dubPing", 2), "Dub Ping-Pong", true));
}

void addDubPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
