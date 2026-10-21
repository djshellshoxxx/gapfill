// State subtree rows owned by the WP4 recipe features (docs/dev/02 F0-6).
// `<Perf>` holds the audition trigger (U-3) and the A/B match switch (F-S06);
// later rows (`<Lock>`, `<History>`) are added by their tickets below.

#include "RecipeServices.h"
#include "../../PluginProcessor.h"

namespace gapfill::recipes
{

bool validatePerf (const juce::ValueTree& perf)
{
    return audition::MidiTrigger::isValid (perf);
}

void applyPerf (GapFillAudioProcessor& proc, const juce::ValueTree& perf)
{
    proc.getRecipes().trigger.readFrom (perf);
}

juce::ValueTree serialisePerf (const GapFillAudioProcessor& proc)
{
    const auto& t = proc.getRecipes().trigger;

    if (t.getNote() < 0 && t.getChannel() == 0 && t.getCC() < 0)
        return {};                                   // all defaults: write nothing

    juce::ValueTree perf ("Perf");
    t.writeTo (perf);
    return perf;
}

void resetPerf (GapFillAudioProcessor& proc)
{
    proc.getRecipes().trigger.reset();
}

} // namespace gapfill::recipes
