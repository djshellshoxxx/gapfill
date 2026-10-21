#include "Roulette.h"

#include "../../PluginProcessor.h"
#include "../../Presets.h"

namespace gapfill::live
{

GlitchRoulette::Result GlitchRoulette::spin (GapFillAudioProcessor& proc)
{
    proc.pushUndoSnapshot();

    juce::String name;
    last = surpriseParameters (proc.getValueTreeState(), rng, last, name);   // never repeats `last`
    lastName = name;
    proc.updateHostDisplay();

    return { last, lastName };
}

} // namespace gapfill::live
