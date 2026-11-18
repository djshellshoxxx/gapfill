#pragma once

// F-X06 extensions are a NEW tail group. Never append these to released COOP:
// that would shift later host indices and change old automation normalization.
#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_COOPEXT(X) \
    X (coopDeterministic, "coopDeterministic") \
    X (coopDetGroup, "coopDetGroup") \
    X (coopSlot, "coopSlot") \
    X (coopPriority, "coopPriority") \
    X (coopYield, "coopYield") \
    X (coopGrid, "coopGrid")

namespace gapfill
{
void addCoopextParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);
void addCoopextPerformanceIds (juce::StringArray&);
}
