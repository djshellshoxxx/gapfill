#pragma once

// Parameter group "personality" (docs/dev/02-FOUNDATIONS.md F0-1).
// Owner: C-1 (personality).
//
// The group's parameters are listed in GAPFILL_PARAMS_PERSONALITY below as X-macro
// entries (member name, then permanent string id) joined by line
// continuations. IDs are permanent and append-only: mirror every addition in
// tools/param_ids/personality.lock (tools/validate_repo.py enforces both), build the
// parameter in params/personality.cpp and classify it for Random/Surprise/Undo there.
//
// Group order in Parameters.h fixes parameter index order and is permanent.

#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_PERSONALITY(X) \
    X (personality, "personality")

namespace gapfill
{

/** Appends this group's parameters to the layout (called once, in group order). */
void addPersonalityParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

/** Appends the ids of this group that are performance state and must be
    excluded from Random / Random All / Surprise / Undo snapshots. */
void addPersonalityPerformanceIds (juce::StringArray& ids);

} // namespace gapfill
