#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// New tail only. The released hgAmount/hgVoices/hgMotion/hgTone registry and
// normalization remain unchanged; this opt-in path follows the detailed ticket.
#define GAPFILL_PARAMS_HGEXT(X) \
    X (fx01_mode, "fx01_mode") \
    X (fx01_intervals, "fx01_intervals") \
    X (fx01_voices, "fx01_voices") \
    X (fx01_motion, "fx01_motion") \
    X (fx01_scatter, "fx01_scatter") \
    X (fx01_tone, "fx01_tone") \
    X (fx01_snap, "fx01_snap")
namespace gapfill
{
void addHgextParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);
void addHgextPerformanceIds (juce::StringArray&);
void addHgextRandomAllIds (juce::StringArray&, bool enabled);
}
