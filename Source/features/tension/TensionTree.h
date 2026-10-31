#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::tension
{
class TensionService;

bool validateTree (const juce::ValueTree&);
void applyTree (GapFillAudioProcessor&, const juce::ValueTree&);
/** Non-RT persistence adapter. A failed capture returns __serialiseError. */
juce::ValueTree serialiseTree (const TensionService&);
juce::ValueTree serialiseTree (const GapFillAudioProcessor&);
void resetTree (GapFillAudioProcessor&);

} // namespace gapfill::tension
