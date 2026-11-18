#pragma once

// F-L12 additive routing controls. Append WETBUSEXT at the permanent registry
// tail; never insert these parameters beside the already shipped WETOUT group.

#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_WETBUSEXT(X) \
    X (wetBusMainMode, "wetBusMainMode") \
    X (wetBusLevel, "wetBusLevel")

namespace gapfill
{
void addWetbusextParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);
void addWetbusextPerformanceIds (juce::StringArray&);
} // namespace gapfill
