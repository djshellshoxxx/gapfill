#include "formant.h"
#include "ParamBuilders.h"
#include "../Parameters.h"

namespace gapfill
{

// F-L20 Formant shifting (docs/dev/12 section 7). Neutral = fmtAmount 0.
void addFormantParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APF> (makeID ("fmtAmount", 2), "Formant Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("fmtShift", 2), "Formant Shift",
                                       juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("st")));
    layout.add (std::make_unique<APF> (makeID ("fmtTilt", 2), "Formant Tilt",
                                       juce::NormalisableRange<float> (-100.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APC> (makeID ("fmtVowel", 2), "Formant Vowel",
                                       juce::StringArray { "Shift only", "A", "E", "I", "O", "U" }, 0));
}

void addFormantPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
