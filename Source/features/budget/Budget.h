#pragma once

// F-D01 Per-bar fill budget and quiet zones (docs/dev/12 F-D01).
//
// Evaluated in the scheduler's eligibility stage, before any RNG draw (so a
// skipped gap draws nothing and no other gap is perturbed). Both settings Off
// is the identity. Needs a valid PPQ and a known time signature; without them
// the feature is neutral (and telemetry reports "needs transport").
//
// Budget: the number of fills started in the CURRENT bar is counted from a ring of
// the last 16 fill start ticks (ppq * 96), so loops and seeks need no incremental
// counter. The ring is cleared on a transport jump, so a seek back recounts.
// Quiet zone: a fill may not START inside the zone; a running fill continues.

#include "../../dsp/SchedulerTypes.h"
#include "../effective/Effective.h"

#include <array>
#include <cstdint>

namespace gapfill::budget
{

inline constexpr int kBudgetChoices[7] { 0, 1, 2, 3, 4, 6, 8 };   // fillBudget choice index -> fills per bar (0 = Off)

enum Zone { ZoneOff = 0, Beat1, LastBeat, LastTwoBeats, LastBarOfPhrase, NumZones };

inline constexpr int kTicksPerBeat = 96;

/** The last 16 FillStarted positions as ppq * 96 ticks. Audio-thread owned. */
class RecentFills
{
public:
    static constexpr int capacity = 16;

    void push (std::int64_t tick) noexcept;
    void clear() noexcept   { count = 0; head = 0; }
    int  size() const noexcept { return count; }

    /** Entries whose bar index (anchored at PPQ 0) equals `barIdx`. */
    int countInBar (long long barIdx, double beatsPerBar) const noexcept;

private:
    std::array<std::int64_t, capacity> ticks {};
    int count = 0;
    int head = 0;
};

inline std::int64_t tickFor (double ppq) noexcept;

/** Position inside the current bar in quarter-note beats, [0, beatsPerBar). */
double positionInBar (double ppq, double beatsPerBar) noexcept;

/** True when `ppq` lies inside the quiet zone (false for Off / unusable phrase). */
bool inQuietZone (int zone, double ppq, double beatsPerBar, const PhraseSettings&,
                  const Effective::TransportInfo&) noexcept;

/** The F-D01 eligibility verdict: None, BudgetFull or QuietZone. `needsTransport`
    (if given) is set when a setting is on but the transport / time signature cannot support it. */
RejectReason evaluate (const BudgetSettings&, const PhraseSettings&, double ppq,
                       const Effective::TransportInfo&, const RecentFills&,
                       bool* needsTransport = nullptr) noexcept;

inline std::int64_t tickFor (double ppq) noexcept
{
    return (std::int64_t) (ppq * (double) kTicksPerBeat + (ppq < 0.0 ? -0.5 : 0.5));
}

} // namespace gapfill::budget
