#include "fl20.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addFl20Parameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    layout.add (std::make_unique<APF> (makeID ("fl20_amount", 3), "Voice Formant Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f, Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("fl20_shift", 3), "Voice Formant Shift",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, Attr().withLabel ("st")));
    layout.add (std::make_unique<APF> (makeID ("fl20_tilt", 3), "Voice Formant Tilt",
        juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f, Attr().withLabel ("dB/oct")));
    layout.add (std::make_unique<APC> (makeID ("fl20_vowel", 3), "Voice Formant Vowel",
        juce::StringArray { "Off", "A", "E", "I", "O", "U" }, 0));
    layout.add (std::make_unique<APF> (makeID ("fl20_preserve", 3), "Voice Formant Preserve",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, Attr().withLabel ("%")));
    layout.add (std::make_unique<APC> (makeID ("fl20_detail", 3), "Voice Formant Detail",
        juce::StringArray { "Low", "Medium", "High" }, 1));
}

// Random's explicit allow-list does not include FL20. Normal Undo/A-B retain it.
void addFl20PerformanceIds (juce::StringArray&) {}

} // namespace gapfill
