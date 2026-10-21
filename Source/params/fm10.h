#pragma once

// Parameter group "fm10" (docs/dev/features/F-M10-momentary-effects-and-latch.md section 4).
// Owner: F-M10 momentary / latch performance holds. IDs are permanent and append-only
// (tools/param_ids/fm10.lock). The four hold bools are momentary state: they are
// performance parameters and are forced off after any state restore.

#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_FM10(X) \
    X (fm10_stutter,  "fm10_stutter") \
    X (fm10_freeze,   "fm10_freeze") \
    X (fm10_reverse,  "fm10_reverse") \
    X (fm10_tape,     "fm10_tape") \
    X (fm10_mode,     "fm10_mode") \
    X (fm10_level,    "fm10_level") \
    X (fm10_dryDuck,  "fm10_dryDuck") \
    X (fm10_protect,  "fm10_protect") \
    X (fm10_timeout,  "fm10_timeout")

namespace gapfill
{

void addFm10Parameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
void addFm10PerformanceIds (juce::StringArray& ids);

} // namespace gapfill
