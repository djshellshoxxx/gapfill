#include "tension.h"
#include "ParamBuilders.h"

namespace gapfill
{

// F-M21 Tension / release curves (docs/dev/features/F-M21 section 4). Neutral = fm21_on off.
void addTensionParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APB> (makeID ("fm21_on", 2), "Tension", false));
    layout.add (std::make_unique<APC> (makeID ("fm21_bars", 2), "Tension Bars", juce::StringArray { "2", "4", "8", "16", "32" }, 2));
    layout.add (std::make_unique<APInt> (makeID ("fm21_start", 2), "Tension Start Bar", 0, 255, 0));
    layout.add (std::make_unique<APC> (makeID ("fm21_shape", 2), "Tension Shape",
                                       juce::StringArray { "Build", "Build+Drop", "Breakdown", "Swell", "Custom" }, 1));
    layout.add (std::make_unique<APC> (makeID ("fm21_end", 2), "Tension End", juce::StringArray { "Loop", "Hold", "Release" }, 0));

    const auto depth = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<APF> (makeID (id, 2), name, juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), def, Attr().withLabel ("%")));
    };

    depth ("fm21_depthDensity", "Tension Density Depth", 40.0f);
    depth ("fm21_depthIntensity", "Tension Intensity Depth", 30.0f);
    depth ("fm21_depthWidth", "Tension Width Depth", 0.0f);
    depth ("fm21_depthFx", "Tension FX Depth", 0.0f);
}

void addTensionPerformanceIds (juce::StringArray& ids)
{
    for (const char* id : { "fm21_on", "fm21_bars", "fm21_start", "fm21_shape", "fm21_end", "fm21_depthDensity",
                            "fm21_depthIntensity", "fm21_depthWidth", "fm21_depthFx" })
        ids.add (id);
}

} // namespace gapfill
