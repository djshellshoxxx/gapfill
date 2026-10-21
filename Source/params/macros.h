#pragma once

// Parameter group "macros" (docs/dev/02-FOUNDATIONS.md F0-1).
// Owner: C-2 (creative macros: macroActivity, macroChaos, macroSpace, macroDestruction, macrosOn).
//
// The group's parameters are listed in GAPFILL_PARAMS_MACROS below as X-macro
// entries (member name, then permanent string id) joined by line
// continuations. IDs are permanent and append-only: mirror every addition in
// tools/param_ids/macros.lock (tools/validate_repo.py enforces both), build the
// parameter in params/macros.cpp and classify it for Random/Surprise/Undo there.
//
// Group order in Parameters.h fixes parameter index order and is permanent.

#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_MACROS(X)

namespace gapfill
{

/** Appends this group's parameters to the layout (called once, in group order). */
void addMacrosParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

/** Appends the ids of this group that are performance state and must be
    excluded from Random / Random All / Surprise / Undo snapshots. */
void addMacrosPerformanceIds (juce::StringArray& ids);

} // namespace gapfill
