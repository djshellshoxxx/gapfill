#pragma once

// The `<History>` state subtree (docs/dev/05 U-1, cap 1 KiB):
//
//   <History><R b="base64"/> ... up to 8, newest first </History>
//
// Each entry is a recipe in the RecipeCodec text form. An invalid entry is dropped
// on its own; the rest of the history and of the state still loads. Only recipes
// are stored: never audio, never positions (export spans do not survive a session).

#include <juce_data_structures/juce_data_structures.h>

namespace gapfill
{

class GapFillAudioProcessor;

namespace history
{

bool validate (const juce::ValueTree&);
void apply (GapFillAudioProcessor&, const juce::ValueTree&);
juce::ValueTree serialise (const GapFillAudioProcessor&);
void reset (GapFillAudioProcessor&);

} // namespace history
} // namespace gapfill
