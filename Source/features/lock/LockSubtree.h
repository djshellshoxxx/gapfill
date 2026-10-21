#pragma once

// The `<Lock>` state subtree (docs/dev/04 C-5 "State and threading", cap 1 KiB):
//
//   <Lock valid="1" recipe="base64 (16 recipe bytes + checksum)"/>
//
// The processor keeps a message-thread MIRROR of the engine's lock recipe, updated
// from FillEvent::LockSet / LockCleared, so the lock is saved correctly with the
// editor closed. On load a valid subtree posts LockRecipe (restore cannot trigger
// anything audible: nothing plays until the next natural gap); an absent or invalid
// one posts Unlock, which leaves the lock ARMED when fillLock is on.

#include "../../dsp/FillRecipe.h"

#include <juce_data_structures/juce_data_structures.h>

namespace gapfill
{

class GapFillAudioProcessor;

namespace lock
{

struct LockMirror
{
    bool valid = false;
    FillRecipe recipe;
};

bool validate (const juce::ValueTree&);
void apply (GapFillAudioProcessor&, const juce::ValueTree&);
juce::ValueTree serialise (const GapFillAudioProcessor&);
void reset (GapFillAudioProcessor&);

} // namespace lock
} // namespace gapfill
