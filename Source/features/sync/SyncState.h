#pragma once

// <Perf> state subtree rows owned by E-2 (docs/dev/03 E-2 change 1): the
// non-automatable "Tempo when host provides none" setting. Other tickets that
// need persisted preferences extend the same subtree through their own
// attribute (keep every attribute optional so absent = default).

#include <juce_data_structures/juce_data_structures.h>

namespace gapfill
{

class GapFillAudioProcessor;

namespace sync
{

bool validatePerf (const juce::ValueTree&);
void applyPerf (GapFillAudioProcessor&, const juce::ValueTree&);
juce::ValueTree serialisePerf (const GapFillAudioProcessor&);   ///< invalid tree when everything is default
void resetPerf (GapFillAudioProcessor&);

} // namespace sync
} // namespace gapfill
