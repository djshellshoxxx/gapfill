#pragma once

// Append-only tail group: existing fmt* bus parameters retain their semantics.
#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_FL20(X) \
    X (fl20Amount, "fl20_amount") \
    X (fl20Shift, "fl20_shift") \
    X (fl20Tilt, "fl20_tilt") \
    X (fl20Vowel, "fl20_vowel") \
    X (fl20Preserve, "fl20_preserve") \
    X (fl20Detail, "fl20_detail")

namespace gapfill
{
void addFl20Parameters (juce::AudioProcessorValueTreeState::ParameterLayout&);
void addFl20PerformanceIds (juce::StringArray&);
}
