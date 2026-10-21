#pragma once

// E-3 "Why didn't it fill?" - the text half of the feature (docs/dev/03 E-3).
//
// Pure functions over the engine's own RejectReason / FillEvent records: this
// module never recomputes a reason, it only words it. No GUI, no audio-thread
// code, so every sentence is unit-testable.

#include "../../dsp/EngineEvents.h"
#include "../../dsp/SchedulerTypes.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>

namespace gapfill::reasons
{

struct ReasonInfo
{
    RejectReason reason;
    const char* label;     ///< plain label, e.g. "Gap too short"
    const char* advice;    ///< what to change, e.g. "Lower Min Gap ..."
    char letter;           ///< unique one-letter code used by the timeline glyphs
};

/** One row per RejectReason (None included). Total: out-of-range values map to a
    neutral "Unknown" row, never to undefined behaviour. */
const ReasonInfo& info (RejectReason) noexcept;

inline juce::String label (RejectReason r)    { return info (r).label; }
inline juce::String advice (RejectReason r)   { return info (r).advice; }

/** Reasons whose label is followed by the measured numbers. */
bool showsNumbers (RejectReason) noexcept;

/** "Gap too short (38 ms free, need 60 ms)" or just the label. */
juce::String labelWithNumbers (RejectReason, float availMs, float needMs);

/** Hover text: "<label> - <X> ms free, <Y> ms needed. <Advice>". */
juce::String hoverText (RejectReason, float availMs, float needMs);

/** Footer status line for the newest outcome:
    "Last gap: skipped - Gap too short (38 ms free, need 60 ms)" or "Last gap: filled". */
juce::String statusLine (const FillEvent&);

struct TopReason
{
    RejectReason reason = RejectReason::None;
    std::uint32_t count = 0;
};

/** The `maxOut` most frequent reasons (ties: lower enum value first), skipping
    None and zero counts. `counts[i]` is the session count of RejectReason i.
    Returns how many were written. */
int topReasons (const std::uint32_t* counts, int numCounts, TopReason* out, int maxOut) noexcept;

/** Text of the "Why so few fills?" popover. */
juce::String whySoFewFillsText (const std::uint32_t* counts, int numCounts);

/** Allows at most one status-line update per interval (default 4 Hz). Time is
    injected so the limiter is deterministic under test. */
class StatusRateLimiter
{
public:
    explicit StatusRateLimiter (std::uint32_t minIntervalMs = 250) noexcept : interval (minIntervalMs) {}

    /** True when an update is allowed at `nowMs`; the call consumes the slot. */
    bool allow (std::uint32_t nowMs) noexcept
    {
        if (haveLast && (std::uint32_t) (nowMs - last) < interval)
            return false;

        haveLast = true;
        last = nowMs;
        return true;
    }

private:
    std::uint32_t interval;
    std::uint32_t last = 0;
    bool haveLast = false;
};

} // namespace gapfill::reasons
