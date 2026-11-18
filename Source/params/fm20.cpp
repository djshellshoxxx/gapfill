#include "fm20.h"
#include "ParamBuilders.h"

namespace gapfill
{
void addTapeExtraParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    const auto add = [&] (const char* id, const char* name, float initial)
    {
        layout.add (std::make_unique<APF> (makeID (id, 3), name,
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), initial, Attr().withLabel ("%")));
    };
    add ("fm20_amount", "Tape Damage Chance", 0.0f);
    add ("fm20_wow", "Tape Damage Wow", 30.0f);
    add ("fm20_flutter", "Tape Damage Flutter", 25.0f);
    add ("fm20_sat", "Tape Damage Saturation", 30.0f);
    add ("fm20_dropDensity", "Tape Damage Dropout Density", 20.0f);
    add ("fm20_dropDepth", "Tape Damage Dropout Depth", 60.0f);
    add ("fm20_splice", "Tape Damage Splice", 10.0f);
    add ("fm20_instab", "Tape Damage Instability", 20.0f);
    add ("fm20_mix", "Tape Damage Mix", 100.0f);
}
void addTapeExtraPerformanceIds (juce::StringArray&) {}
} // namespace gapfill
