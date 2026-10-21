#pragma once

// F-M03 Gap timeline - the data half (docs/dev/12 F-M03, docs/dev/10 section 4.1).
//
// TimelineModel keeps the last 512 timeline events on the message thread. It is
// fed by the processor timer from the audio -> message `fillEvents` queue and
// stores the engine's own records: a reason shown on the timeline is the reason
// the engine returned, never one recomputed from UI state.

#include "../../dsp/EngineEvents.h"

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace gapfill::timeline
{

enum class EventType : std::uint8_t
{
    None = 0, Onset, GapOpen, GapEval, FillStart, FillEnd, Preview, SceneApplied, Panic, Recover
};

/** 32 bytes, trivially copyable (spec: extends FillEvent). */
struct TimelineEvent
{
    EventType type = EventType::None;
    std::uint8_t reason = 0;           ///< RejectReason (GapEval)
    std::uint8_t confidence8 = 0;
    std::uint8_t algo = 0xFF;          ///< FillStart
    std::uint16_t lenMs = 0;           ///< FillStart: planned length
    std::uint16_t availMs = 0;         ///< GapEval: room that was available
    std::uint16_t needMs = 0;          ///< GapEval: room that would have been needed
    std::uint16_t protHoldMs = 0;      ///< Onset: protection hold after the event
    std::uint16_t lookaheadMs = 0;     ///< Onset: lookahead window before the event
    std::uint16_t reserved = 0;
    std::uint32_t generation = 0;      ///< engine gap index
    float ppq = -1.0f;                 ///< < 0 when unknown
    std::int64_t pos = 0;              ///< output-timeline sample
};

static_assert (sizeof (TimelineEvent) == 32, "TimelineEvent must stay 32 bytes");
static_assert (std::is_trivially_copyable_v<TimelineEvent>);

/** Converts one engine event into 0..2 timeline events (a started fill is a
    GapEval with no reason plus a FillStart). Returns how many were written to `out`. */
int fromEngineEvent (const FillEvent&, TimelineEvent out[2]) noexcept;

/** One-line text for the keyboard-reachable companion list and the legend. */
juce::String describe (const TimelineEvent&);

/** Total, never throws: every EventType has a name and an explanation. */
const char* typeName (EventType) noexcept;

//==============================================================================
class TimelineModel
{
public:
    static constexpr int capacity = 512;

    /** Message thread. */
    void push (const TimelineEvent&) noexcept;
    void pushEngineEvent (const FillEvent&) noexcept;

    /** The fillEvents queue (or its optional-event shortcut) dropped events:
        everything at or before `pos` of the newest known event may be incomplete. */
    void noteDropped (std::int64_t newestKnownPos) noexcept   { lossUpToPos = newestKnownPos; haveLoss = true; }

    int size() const noexcept                { return count; }

    /** 0 = oldest retained event. */
    const TimelineEvent& at (int i) const noexcept;

    /** Copies the events with pos in (endPos - windowSamples, endPos], oldest first. */
    void window (std::int64_t endPos, std::int64_t windowSamples, std::vector<TimelineEvent>& out) const;

    /** True when a loss happened inside the window (shown as a striped end-cap). */
    bool isIncomplete (std::int64_t endPos, std::int64_t windowSamples) const noexcept
    {
        return haveLoss && lossUpToPos > endPos - windowSamples;
    }

    /** Newest events first, at most `maxItems`, for the companion list. */
    int latest (TimelineEvent* out, int maxItems) const noexcept;

    /** Pause freezes only the DISPLAY: events keep being recorded. */
    void setPaused (bool shouldPause, std::int64_t nowPos) noexcept { paused = shouldPause; frozenPos = nowPos; }
    bool isPaused() const noexcept                                   { return paused; }
    std::int64_t viewEnd (std::int64_t livePos) const noexcept       { return paused ? frozenPos : livePos; }

    void clear() noexcept;

private:
    std::array<TimelineEvent, (size_t) capacity> ring {};
    int head = 0;       // index of the oldest
    int count = 0;
    bool haveLoss = false;
    std::int64_t lossUpToPos = 0;
    bool paused = false;
    std::int64_t frozenPos = 0;
};

} // namespace gapfill::timeline
