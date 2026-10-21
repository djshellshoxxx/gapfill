#pragma once

// F-X03 <Key> state subtree (<= 64 bytes): the last valid estimate, written only while
// Auto key or Hold is on. On load it is a warm start that the detector replaces as soon
// as it has heard tonal material, unless Hold is on (then it is authoritative).
//   <Key tonic="9" mode="1" conf="87" hold="0"/>

#include <juce_data_structures/juce_data_structures.h>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::keydetect
{

bool validateKeyTree (const juce::ValueTree&);
void applyKeyTree (GapFillAudioProcessor&, const juce::ValueTree&);
juce::ValueTree serialiseKeyTree (const GapFillAudioProcessor&);
void resetKeyTree (GapFillAudioProcessor&);

} // namespace gapfill::keydetect
