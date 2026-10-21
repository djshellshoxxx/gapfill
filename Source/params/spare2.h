#pragma once

// Parameter group "spare2" (docs/dev/02-FOUNDATIONS.md F0-1).
// Owner: unclaimed spare group.
//
// The group's parameters are listed in GAPFILL_PARAMS_SPARE2 below as X-macro
// entries (member name, then permanent string id) joined by line
// continuations. IDs are permanent and append-only: mirror every addition in
// tools/param_ids/spare2.lock (tools/validate_repo.py enforces both), build the
// parameter in params/spare2.cpp and classify it for Random/Surprise/Undo there.
//
// Group order in Parameters.h fixes parameter index order and is permanent.

#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_SPARE2(X)

namespace gapfill
{

/** Appends this group's parameters to the layout (called once, in group order). */
void addSpare2Parameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

/** Appends the ids of this group that are performance state and must be
    excluded from Random / Random All / Surprise / Undo snapshots. */
void addSpare2PerformanceIds (juce::StringArray& ids);

} // namespace gapfill
