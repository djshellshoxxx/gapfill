#pragma once

// <Perf> attribute owned by F-L11 (MIDI output): `midiOutOffsetMs`, the Options value that shifts the
// emitted notes (-100..+100 ms, default 0). Optional like every <Perf> attribute: absent = default,
// so projects saved before this attribute existed load unchanged.

#include <juce_data_structures/juce_data_structures.h>

namespace gapfill
{

class GapFillAudioProcessor;

namespace midiout
{

bool validatePerf (const juce::ValueTree&);
void applyPerf (GapFillAudioProcessor&, const juce::ValueTree&);
juce::ValueTree serialisePerf (const GapFillAudioProcessor&);   ///< invalid tree when the offset is 0
void resetPerf (GapFillAudioProcessor&);

} // namespace midiout
} // namespace gapfill
