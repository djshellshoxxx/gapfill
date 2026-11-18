#include "l21ext.h"
#include "ParamBuilders.h"
#include <cmath>

namespace gapfill
{
void addL21extParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    layout.add (std::make_unique<APC> (makeID ("fl21_mode", 3), "Hit Reverb Mode",
        juce::StringArray { "Off", "Hit Reverb" }, 0));
    layout.add (percent ("fl21_mix", "Hit Reverb Mix", 0, 3));
    layout.add (std::make_unique<APC> (makeID ("fl21_source", 3), "Hit Reverb Source",
        juce::StringArray { "Last Hit", "Strongest", "Manual" }, 0));
    layout.add (std::make_unique<APB> (makeID ("fl21_hold", 3), "Hit Reverb Hold", false));
    layout.add (millis ("fl21_length", "Hit Reverb Length", 50, 800, 250, 250, 3));
    layout.add (millis ("fl21_preDelay", "Hit Reverb Pre-delay", 0, 50, 0, 15, 3));
    juce::NormalisableRange<float> dampRange (2000.0f, 20000.0f,
        [] (float start, float end, float proportion) { return start * std::pow (end / start, proportion); },
        [] (float start, float end, float value) { return std::log (value / start) / std::log (end / start); });
    layout.add (std::make_unique<APF> (makeID ("fl21_damp", 3), "Hit Reverb Damp", dampRange, 12000,
        Attr().withLabel ("Hz").withStringFromValueFunction ([] (float value, int)
            { return juce::String (value / 1000.0f, 2) + " kHz"; })
        .withValueFromStringFunction ([] (const juce::String& text)
            { return text.containsIgnoreCase ("k") ? text.getFloatValue() * 1000.0f : text.getFloatValue(); })));
    layout.add (percent ("fl21_decay", "Hit Reverb Decay", 40, 3));
    layout.add (std::make_unique<APC> (makeID ("fl21_stereo", 3), "Hit Reverb Stereo",
        juce::StringArray { "Stereo", "Mono" }, 0));
}
void addL21extPerformanceIds (juce::StringArray&) {}
void addL21extRandomAllIds (juce::StringArray&, bool) {}
} // namespace gapfill
