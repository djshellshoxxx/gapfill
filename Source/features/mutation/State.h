#pragma once

// Function-pointer glue for the "Mutation" row in features/state/StateSubtrees.cpp.

#include "MutationLab.h"

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::mutation
{

bool validateSubtree (const juce::ValueTree&);
void applySubtree (GapFillAudioProcessor&, const juce::ValueTree&);
juce::ValueTree serialiseSubtree (const GapFillAudioProcessor&);
void resetSubtree (GapFillAudioProcessor&);

} // namespace gapfill::mutation
