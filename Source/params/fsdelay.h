#pragma once

// Parameter group "fsdelay" (docs/dev/02-FOUNDATIONS.md F0-1).
// Owner: doc 12 feature group (see docs/dev/12-SPEC-BACKLOG-AND-NEW.md section 2.1 / 02 permanent registry).
//
// The group's parameters are listed in GAPFILL_PARAMS_FSDELAY below as X-macro
// entries (member name, then permanent string id) joined by line
// continuations. IDs are permanent and append-only: mirror every addition in
// tools/param_ids/fsdelay.lock (tools/validate_repo.py enforces both), build the
// parameter in params/fsdelay.cpp and classify it for Random/Surprise/Undo there.
//
// Group order in Parameters.h fixes parameter index order and is permanent.

#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_FSDELAY(X) \
    X (fsdAmount, "fsdAmount") \
    X (fsdShift, "fsdShift") \
    X (fsdTime, "fsdTime") \
    X (fsdFeedback, "fsdFeedback") \
    X (fsdDamp, "fsdDamp") \
    X (fsdStereo, "fsdStereo")

namespace gapfill
{

/** Appends this group's parameters to the layout (called once, in group order). */
void addFsdelayParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

/** Appends the ids of this group that are performance state and must be
    excluded from Random / Random All / Surprise / Undo snapshots. */
void addFsdelayPerformanceIds (juce::StringArray& ids);

} // namespace gapfill
