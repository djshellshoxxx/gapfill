#pragma once

// Parameter group "bandtype" (docs/dev/02-FOUNDATIONS.md F0-1).
// Owner: C-9 (bandLowType, bandMidType, bandHighType).
//
// The group's parameters are listed in GAPFILL_PARAMS_BANDTYPE below as X-macro
// entries (member name, then permanent string id) joined by line
// continuations. IDs are permanent and append-only: mirror every addition in
// tools/param_ids/bandtype.lock (tools/validate_repo.py enforces both), build the
// parameter in params/bandtype.cpp and classify it for Random/Surprise/Undo there.
//
// Group order in Parameters.h fixes parameter index order and is permanent.

#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_BANDTYPE(X) \
    X (bandLowType,  "bandLowType") \
    X (bandMidType,  "bandMidType") \
    X (bandHighType, "bandHighType")

namespace gapfill
{

/** Appends this group's parameters to the layout (called once, in group order). */
void addBandtypeParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

/** Appends the ids of this group that are performance state and must be
    excluded from Random / Random All / Surprise / Undo snapshots. */
void addBandtypePerformanceIds (juce::StringArray& ids);

} // namespace gapfill
