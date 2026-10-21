#pragma once

// Facade the processor holds for the audition / lock / history / export /
// A-B-compare features (work package WP4). The processor owns ONE instance and
// calls the hooks below; each feature's logic lives in its own directory
// (docs/architecture/MODULES.md, "WP4 recipe features").
//
// Threading: members are tagged. `onMidi`, `afterWet`, `matchGain` run on the
// audio thread; everything else on the message thread.

#include "../audition/MidiTrigger.h"
#include "../lock/LockSubtree.h"
#include "../../dsp/EngineEvents.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

namespace gapfill
{

class FillEngine;
class GapFillAudioProcessor;

namespace recipes
{

class RecipeServices
{
public:
    audition::MidiTrigger trigger;      // E-1 / U-3
    lock::LockMirror lockMirror;        // C-5: message-thread copy of the engine's lock recipe

    //==========================================================================
    // Audio thread

    /** Once per processBlock, after the MIDI-learn scan: fires the audition
        trigger. No allocation, no locks. */
    void onMidi (FillEngine&, const juce::MidiBuffer&) noexcept;

    //==========================================================================
    // Message thread

    /** 15 Hz processor timer. Returns true when something visible changed. */
    bool tick() noexcept;

    /** Every audio -> message fill event, called from GapFillAudioProcessor::drainFillEvents
        (message thread) before the history / outcome log see it. Returns true when the
        event belongs to a feature (preview, lock) and must not enter the outcome log. */
    bool onFillEvent (const FillEvent&) noexcept;

    /** A one-line status message shown in the footer for `durationMs` (the
        editor's AUDITION button and the recipe strip post here). Message thread only. */
    void postMessage (const juce::String& text, std::uint32_t durationMs = 2000);

    /** The message currently on show, or an empty string once it has expired. */
    juce::String getMessage() const;

private:
    juce::String message;
    std::uint32_t messageUntilMs = 0;
};

//------------------------------------------------------------------------------
// `<Perf>` subtree rows (StateSubtrees.cpp): trigger note / channel / CC and the
// F-S06 "Match levels" switch. Not host-automatable, never serialised at defaults.
bool validatePerf (const juce::ValueTree&);
void applyPerf (GapFillAudioProcessor&, const juce::ValueTree&);
juce::ValueTree serialisePerf (const GapFillAudioProcessor&);
void resetPerf (GapFillAudioProcessor&);

} // namespace recipes
} // namespace gapfill
