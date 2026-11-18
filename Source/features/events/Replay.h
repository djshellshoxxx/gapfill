#pragma once

// F-D02 replay harness (docs/dev/12 F-D02). Re-injects a recording into a fresh
// processor at the recorded block boundaries, with SUPPLIED audio, and reports
// the first place where the sequence of gap outcomes differs from the recording.
// Used by the Standalone "Open replay..." action and by the regression tests.

#include "EventRecorder.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace gapfill::events
{

/** What a replay compares: whether a gap was filled, why not, and where it was decided. */
struct OutcomeKey
{
    bool started = false;
    std::uint8_t reason = 0;
    std::int64_t pos = 0;
    bool detailed = false;
    FillRecipe recipe {};
    RecipeExtras extras {};

    bool operator== (const OutcomeKey& o) const noexcept
    {
        if (started != o.started || reason != o.reason || pos != o.pos || detailed != o.detailed)
            return false;
        if (! detailed)
            return true;
        std::uint8_t first[FillRecipe::serialisedBytes], second[FillRecipe::serialisedBytes];
        gapfill::toBytes (recipe, first);
        gapfill::toBytes (o.recipe, second);
        return std::memcmp (first, second, sizeof first) == 0 && extras == o.extras;
    }
};

std::vector<OutcomeKey> outcomesOf (const ReplayData&);
namespace detail { OutcomeKey outcomeKeyAt (const ReplayData&, std::size_t recordIndex); }

/** Index of the first differing outcome, or -1 when the sequences are identical
    (a length difference counts as a divergence at the shorter length). */
int findFirstDivergence (const std::vector<OutcomeKey>& expected, const std::vector<OutcomeKey>& actual) noexcept;

struct ReplayReport
{
    bool ran = false;
    juce::String error;
    int blocksReplayed = 0;
    std::vector<OutcomeKey> outcomes;     ///< what the replay produced
    int firstDivergence = -1;             ///< versus the recording; -1 = identical
    juce::String divergenceText;          ///< human-readable first difference ("" when identical)
};

/** Runs the recording through a fresh GapFillAudioProcessor. `audio` is read from
    sample 0 in the recorded block sizes; when it is shorter than the recording the
    replay stops there. */
ReplayReport replay (const ReplayData&, const juce::AudioBuffer<float>& audio);

} // namespace gapfill::events
