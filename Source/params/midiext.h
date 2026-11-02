#pragma once

// Additive F-L11 event scheduler parameters. The original midiout group and all its host indices/ranges stay fixed.
// Register this group only at the reviewed global tail; every id is routing/performance state.

#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_MIDIEXT(X) \
    X (midiExtOn, "fl11_on") \
    X (midiExtChan, "fl11_chan") \
    X (midiExtHitOn, "fl11_hit_on") \
    X (midiExtGapOn, "fl11_gap_on") \
    X (midiExtFillOn, "fl11_fill_on") \
    X (midiExtNoteHit, "fl11_note_hit") \
    X (midiExtNoteGap, "fl11_note_gap") \
    X (midiExtNoteFill, "fl11_note_fill") \
    X (midiExtVel, "fl11_vel") \
    X (midiExtLengthMs, "fl11_len_ms") \
    X (midiExtCC, "fl11_cc") \
    X (midiExtTiming, "fl11_timing")

namespace gapfill
{
void addMidiextParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);
void addMidiextPerformanceIds (juce::StringArray&);
}
