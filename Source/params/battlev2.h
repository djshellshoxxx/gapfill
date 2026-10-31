#pragma once

// Additive F-M18 controls. Existing battleEvery keeps its permanent identity.
// Only the coordinator appends this group at the registered parameter tail.
#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_BATTLEV2(X) \
    X (fm18_on, "fm18_on") \
    X (fm18_unit, "fm18_unit") \
    X (fm18_first, "fm18_first")

namespace gapfill
{
void addBattlev2Parameters (juce::AudioProcessorValueTreeState::ParameterLayout&);
void addBattlev2PerformanceIds (juce::StringArray&);
} // namespace gapfill
