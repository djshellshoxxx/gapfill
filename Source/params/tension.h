#pragma once

// Parameter group "tension" (docs/dev/02-FOUNDATIONS.md F0-1).
// Owner: doc 12 feature group (see docs/dev/12-SPEC-BACKLOG-AND-NEW.md section 2.1 / 02 permanent registry).
//
// The group's parameters are listed in GAPFILL_PARAMS_TENSION below as X-macro
// entries (member name, then permanent string id) joined by line
// continuations. IDs are permanent and append-only: mirror every addition in
// tools/param_ids/tension.lock (tools/validate_repo.py enforces both), build the
// parameter in params/tension.cpp and classify it for Random/Surprise/Undo there.
//
// Group order in Parameters.h fixes parameter index order and is permanent.

#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_TENSION(X) \
    X (fm21_on, "fm21_on") \
    X (fm21_bars, "fm21_bars") \
    X (fm21_start, "fm21_start") \
    X (fm21_shape, "fm21_shape") \
    X (fm21_end, "fm21_end") \
    X (fm21_depthDensity, "fm21_depthDensity") \
    X (fm21_depthIntensity, "fm21_depthIntensity") \
    X (fm21_depthWidth, "fm21_depthWidth") \
    X (fm21_depthFx, "fm21_depthFx")

namespace gapfill
{

/** Appends this group's parameters to the layout (called once, in group order). */
void addTensionParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

/** Appends the ids of this group that are performance state and must be
    excluded from Random / Random All / Surprise / Undo snapshots. */
void addTensionPerformanceIds (juce::StringArray& ids);

} // namespace gapfill
