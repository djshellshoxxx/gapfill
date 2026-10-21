#pragma once

// The one status line of the WP4 features (docs/dev/11: "show only the highest-priority
// live message"). It is shown next to the RECIPES handle (RecipeDrawer) rather than in the
// footer, which belongs to the WP3 health / fix-it status component.
//
// Priority, highest first:
//   1. MIDI learn countdown for the audition trigger
//   2. a transient message (rejections for 2 s, "Start playback to audition", save/load results)
//   3. "Preview: <Algorithm>" while an audition plays
//   3b. "+1.4 dB match" while A/B level matching is trimming the output
//   4. "Locked: <recipe>" / "Lock armed" / "Fill Type overrides Lock"
//   5. "LIKED x3"
//
// Message thread only; reads atomics and the processor's message-thread mirrors.

#include <juce_core/juce_core.h>

namespace gapfill
{

class GapFillAudioProcessor;

namespace recipes
{

class RecipeStatus
{
public:
    explicit RecipeStatus (GapFillAudioProcessor&);

    struct Line
    {
        juce::String text;
        bool warning = false;       ///< amber instead of green

        bool operator== (const Line& o) const noexcept   { return text == o.text && warning == o.warning; }
    };

    /** Recomputes the line. Posts a transient message when a new audition rejection appears. */
    Line update();

    /** "Locked: ..." / "Lock armed: ..." / "Fill Type overrides Lock" / empty when Lock is off. */
    static juce::String lockSummary (GapFillAudioProcessor&, bool& isWarning);

private:
    GapFillAudioProcessor& proc;
    std::uint32_t lastSeq = 0;
};

} // namespace recipes
} // namespace gapfill
