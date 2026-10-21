#include "groove.h"
#include "ParamBuilders.h"

namespace gapfill
{

// F-L09 Groove inheritance (docs/dev/features/F-L09 section 4). Neutral = fl09_amount 0.
void addGrooveParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    layout.add (std::make_unique<APF> (makeID ("fl09_amount", 2), "Groove Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APC> (makeID ("fl09_grid", 2), "Groove Grid",
                                       juce::StringArray { "1/8", "1/16" }, 1));
    layout.add (std::make_unique<APB> (makeID ("fl09_hold", 2), "Groove Hold", false));
}

// Performance state: excluded from Random / Surprise / Undo snapshots.
void addGroovePerformanceIds (juce::StringArray& ids)
{
    ids.add ("fl09_amount");
    ids.add ("fl09_grid");
    ids.add ("fl09_hold");
}

} // namespace gapfill
