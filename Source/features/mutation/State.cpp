// F-L17 state subtree glue: the "Mutation" row of the reserved subtree table (cap 2 KiB).

#include "State.h"

#include "../../PluginProcessor.h"

namespace gapfill::mutation
{

bool validateSubtree (const juce::ValueTree& t)
{
    return validateMutationSubtree (t);
}

void applySubtree (GapFillAudioProcessor& proc, const juce::ValueTree& t)
{
    applyMutationSubtree (proc.getUiState().lab, t);
}

juce::ValueTree serialiseSubtree (const GapFillAudioProcessor& proc)
{
    return serialiseMutationSubtree (proc.getUiState().lab);
}

void resetSubtree (GapFillAudioProcessor& proc)
{
    resetMutationSubtree (proc.getUiState().lab);
}

} // namespace gapfill::mutation
