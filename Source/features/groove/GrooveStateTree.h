#pragma once

// F-L09 <Groove> state subtree: a held / learned groove survives save and reload.
//   <Groove v="1" hold="0|1" conf="0.74" o="0.00,0.31,..."/>   (8 offsets in grid steps)
// Written only while the groove is frozen (Hold on, or after Learn); validated
// ranges; a bad subtree is dropped and the tracker restarts.

#include <juce_data_structures/juce_data_structures.h>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::groove
{

bool validateTree (const juce::ValueTree&);
void applyTree (GapFillAudioProcessor&, const juce::ValueTree&);
juce::ValueTree serialiseTree (const GapFillAudioProcessor&);
void resetTree (GapFillAudioProcessor&);

} // namespace gapfill::groove
