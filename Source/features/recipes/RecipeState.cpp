// State subtree rows owned by the WP4 recipe features (docs/dev/02 F0-6).
// `<Perf>` holds the audition trigger (U-3) and the A/B match switch (F-S06);
// later rows (`<Lock>`, `<History>`) are added by their tickets below.

#include "RecipeServices.h"
#include "../../PluginProcessor.h"

namespace gapfill::recipes
{

bool validatePerf (const juce::ValueTree& perf)
{
    if (! audition::MidiTrigger::isValid (perf))
        return false;

    if (perf.hasProperty ("match"))
    {
        const auto v = perf.getProperty ("match").toString().trim();

        if (v != "0" && v != "1")
            return false;
    }

    return true;
}

void applyPerf (GapFillAudioProcessor& proc, const juce::ValueTree& perf)
{
    proc.getRecipes().trigger.readFrom (perf);
    proc.getRecipes().match.setEnabled (perf.getProperty ("match", 0).toString().trim() == "1");
}

juce::ValueTree serialisePerf (const GapFillAudioProcessor& proc)
{
    const auto& t = proc.getRecipes().trigger;
    const auto matching = proc.getRecipes().match.isEnabled();

    if (t.getNote() < 0 && t.getChannel() == 0 && t.getCC() < 0 && ! matching)
        return {};                                   // all defaults: write nothing

    juce::ValueTree perf ("Perf");
    t.writeTo (perf);

    if (matching)
        perf.setProperty ("match", 1, nullptr);     // only the switch is state: the trim is never saved

    return perf;
}

void resetPerf (GapFillAudioProcessor& proc)
{
    proc.getRecipes().trigger.reset();
    proc.getRecipes().match.setEnabled (false);
}

} // namespace gapfill::recipes
