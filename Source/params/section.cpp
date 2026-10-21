#include "section.h"
#include "ParamBuilders.h"

namespace gapfill
{

// F-X04 Multi-section arrangement awareness (docs/dev/features/F-X04 section 4). Neutral = fx04_on off.
void addSectionParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APB> (makeID ("fx04_on", 2), "Arrangement", false));

    const auto pct = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<APF> (makeID (id, 2), name, juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), def, Attr().withLabel ("%")));
    };

    pct ("fx04_calm", "Arrangement Calm", 50.0f);
    pct ("fx04_lift", "Arrangement Lift", 50.0f);
    pct ("fx04_settle", "Arrangement Settle", 30.0f);
    pct ("fx04_sensitivity", "Arrangement Sensitivity", 50.0f);
}

void addSectionPerformanceIds (juce::StringArray& ids)
{
    ids.add ("fx04_on");
}

} // namespace gapfill
