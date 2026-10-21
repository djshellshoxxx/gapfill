#include "fsdelay.h"
#include "ParamBuilders.h"
#include "../Parameters.h"

namespace gapfill
{

// F-L19 Frequency-shifted delay (docs/dev/12 section 7). Neutral = fsdAmount 0.
void addFsdelayParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APF> (makeID ("fsdAmount", 2), "FS Delay Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("fsdShift", 2), "FS Delay Shift",
                                       juce::NormalisableRange<float> (-2000.0f, 2000.0f, 0.1f), 50.0f,
                                       Attr().withLabel ("Hz")));
    layout.add (std::make_unique<APC> (makeID ("fsdTime", 2), "FS Delay Time", divisionNames(), 3));
    layout.add (std::make_unique<APF> (makeID ("fsdFeedback", 2), "FS Delay Feedback",
                                       juce::NormalisableRange<float> (0.0f, 85.0f, 0.01f), 40.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("fsdDamp", 2), "FS Delay Damping",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 30.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("fsdStereo", 2), "FS Delay Stereo",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 40.0f,
                                       Attr().withLabel ("%")));
}

void addFsdelayPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
