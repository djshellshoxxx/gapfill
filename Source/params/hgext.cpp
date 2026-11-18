#include "hgext.h"
#include "ParamBuilders.h"
namespace gapfill
{
void addHgextParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    layout.add (std::make_unique<APC> (makeID ("fx01_mode", 3), "Pitch Sync Mode", juce::StringArray { "Off", "Harmonic" }, 0));
    layout.add (std::make_unique<APC> (makeID ("fx01_intervals", 3), "Harmonic Intervals",
        juce::StringArray { "Octaves", "Open fifths", "Major", "Minor", "Suspended", "Scale" }, 1));
    layout.add (std::make_unique<APInt> (makeID ("fx01_voices", 3), "Pitch Sync Voices", 1, 4, 2));
    layout.add (percent ("fx01_motion", "Pitch Sync Motion", 30, 3));
    layout.add (percent ("fx01_scatter", "Pitch Sync Scatter", 20, 3));
    layout.add (std::make_unique<APF> (makeID ("fx01_tone", 3), "Pitch Sync Tone",
        juce::NormalisableRange<float> (-100, 100, 0.01f), 0, Attr().withLabel ("%")));
    layout.add (percent ("fx01_snap", "Pitch Sync Key Snap", 0, 3));
}
void addHgextPerformanceIds (juce::StringArray& ids) { ids.add ("fx01_mode"); }
void addHgextRandomAllIds (juce::StringArray& ids, bool enabled)
{
    if (enabled) ids.addArray (juce::StringArray { "fx01_motion", "fx01_scatter", "fx01_tone" });
}
} // namespace gapfill
