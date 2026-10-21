#pragma once

// Parameter group "evolve" (docs/dev/02-FOUNDATIONS.md F0-1).
// Owner: C-8 (evolve).
//
// The group's parameters are listed in GAPFILL_PARAMS_EVOLVE below as X-macro
// entries (member name, then permanent string id) joined by line
// continuations. IDs are permanent and append-only: mirror every addition in
// tools/param_ids/evolve.lock (tools/validate_repo.py enforces both), build the
// parameter in params/evolve.cpp and classify it for Random/Surprise/Undo there.
//
// Group order in Parameters.h fixes parameter index order and is permanent.

#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_EVOLVE(X)

namespace gapfill
{

/** Appends this group's parameters to the layout (called once, in group order). */
void addEvolveParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

/** Appends the ids of this group that are performance state and must be
    excluded from Random / Random All / Surprise / Undo snapshots. */
void addEvolvePerformanceIds (juce::StringArray& ids);

} // namespace gapfill
