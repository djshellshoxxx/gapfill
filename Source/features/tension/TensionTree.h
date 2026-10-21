#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::tension
{

bool validateTree (const juce::ValueTree&);
void applyTree (GapFillAudioProcessor&, const juce::ValueTree&);
juce::ValueTree serialiseTree (const GapFillAudioProcessor&);
void resetTree (GapFillAudioProcessor&);

} // namespace gapfill::tension
