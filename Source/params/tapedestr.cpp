#include "tapedestr.h"
#include "ParamBuilders.h"

namespace gapfill
{

// F-M20 Tape destruction (docs/dev/12 section 7). Neutral = tdAmount 0.
void addTapedestrParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    auto pct = [&layout] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<APF> (makeID (id, 2), name,
                                           juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), def,
                                           Attr().withLabel ("%")));
    };

    pct ("tdAmount",  "Tape Destruction Amount", 0.0f);
    pct ("tdWow",     "Tape Wow",                30.0f);
    pct ("tdFlutter", "Tape Flutter",            30.0f);
    pct ("tdSat",     "Tape Saturation",         20.0f);
    pct ("tdDropout", "Tape Dropout",            20.0f);
    pct ("tdSplice",  "Tape Splice",             0.0f);
}

void addTapedestrPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
