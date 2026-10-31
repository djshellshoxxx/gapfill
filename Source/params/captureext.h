#pragma once

// F-M08 additive compatibility switch and Scan. The permanent CAPTURE2 Length
// parameter stays in its released group/index. Only the coordinator registers
// CAPTUREEXT at the current host-parameter tail.
#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_CAPTUREEXT(X) \
    X (fm08_detail, "fm08_detail") \
    X (fm08_scan, "fm08_scan")

namespace gapfill
{
void addCaptureextParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);
void addCaptureextPerformanceIds (juce::StringArray&);
} // namespace gapfill
