#pragma once

// F-X05 <Taste> state subtree (<= 160 bytes): the profile a project renders with, so a saved
// project sounds the same on every machine.
//   <Taste v="1" likes="5" skips="4" ready="1" q="0,12,-8,..."/>      (18 int8 quantised log multipliers)

#include <juce_data_structures/juce_data_structures.h>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::taste
{

bool validateTasteTree (const juce::ValueTree&);
void applyTasteTree (GapFillAudioProcessor&, const juce::ValueTree&);
juce::ValueTree serialiseTasteTree (const GapFillAudioProcessor&);
void resetTasteTree (GapFillAudioProcessor&);

} // namespace gapfill::taste
