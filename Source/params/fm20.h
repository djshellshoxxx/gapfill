#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace gapfill
{
void addTapeExtraParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);
void addTapeExtraPerformanceIds (juce::StringArray&);
}

#define GAPFILL_PARAMS_TAPEXTRA(X) \
    X (tapeExtraAmount, "fm20_amount") \
    X (tapeExtraWow, "fm20_wow") \
    X (tapeExtraFlutter, "fm20_flutter") \
    X (tapeExtraSat, "fm20_sat") \
    X (tapeExtraDropDensity, "fm20_dropDensity") \
    X (tapeExtraDropDepth, "fm20_dropDepth") \
    X (tapeExtraSplice, "fm20_splice") \
    X (tapeExtraInstab, "fm20_instab") \
    X (tapeExtraMix, "fm20_mix")
