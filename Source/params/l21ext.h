#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Tail group only. Root owns registration after CaptureEXT, indices286..294.
#define GAPFILL_PARAMS_L21EXT(X) \
    X (fl21_mode, "fl21_mode") \
    X (fl21_mix, "fl21_mix") \
    X (fl21_source, "fl21_source") \
    X (fl21_hold, "fl21_hold") \
    X (fl21_length, "fl21_length") \
    X (fl21_preDelay, "fl21_preDelay") \
    X (fl21_damp, "fl21_damp") \
    X (fl21_decay, "fl21_decay") \
    X (fl21_stereo, "fl21_stereo")

namespace gapfill
{
void addL21extParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);
void addL21extPerformanceIds (juce::StringArray&);
void addL21extRandomAllIds (juce::StringArray&, bool enabled);
}
