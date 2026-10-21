#pragma once

// F-L13 Sidechain-driven fills (docs/dev/12 F-L13).
//
// SidechainOnsetTracker: a light RMS follower (5 ms window) over the mono
// sidechain that keeps (a) a ring of per-slot levels, so the engine can ask "how
// loud was the sidechain at this output sample", and (b) the last 8 onsets: a
// slot whose level is more than 6 dB above the lowest level of the previous 40 ms
// and above `scThresh`. It only runs while scSnap > 0 or scGate != Off AND the
// sidechain bus is connected; otherwise process() is never called (zero cost) and
// the engine receives no tracker (both controls inert).
//
// Positions are on the engine's output timeline, which is the INPUT timeline of
// the audio the dry path is emitting: the processor feeds the tracker with the
// current input chunk before the engine runs, so the tracker always knows the
// sidechain up to `lookahead` samples ahead of the engine's decision sample.
//
// Gate: "Fill when SC quiet" qualifies a gap only if the level at the decision
// sample is below the threshold; "Fill when SC active" only if it is at or above;
// a closed gate rejects with ScGated, before any RNG draw.
// Snap: moves the evaluation of a gap to the nearest SC onset that lies in
// [decision, decision + 30 ms x snap] (never earlier than the gap start), when
// that still leaves >= minGap and the onset is inside the available lookahead.
// Evaluations happen on the analysis hop grid, so the snapped start lands on the
// hop boundary nearest to the onset (<= hop/2 = 5.3 ms at 48 kHz).
//
// Contract: prepare()/reset() off the audio thread; everything else is wait-free,
// allocation-free. Read-only queries are used from the same (audio) thread.

#include "ScSettings.h"
#include "../../dsp/SchedulerTypes.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gapfill::scdrive
{

inline constexpr float kSnapWindowMs = 30.0f;
inline constexpr float kOnsetRiseDb = 6.0f;
inline constexpr float kFloorMs = 40.0f;
inline constexpr float kRmsMs = 5.0f;
inline constexpr float kRefractoryMs = 50.0f;
inline constexpr float kSilenceDb = -120.0f;

class SidechainOnsetTracker
{
public:
    static constexpr int kSlot = 32;          ///< samples per level slot
    static constexpr int kSlots = 8192;       ///< ring length (262,144 samples = 0.68 s at 384 kHz)
    static constexpr int kOnsets = 8;

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Feeds `n` mono samples that start at `inputPos` on the input timeline. A gap in
        the stream (the feature was off) restarts the follower. */
    void process (const float* mono, int n, std::int64_t inputPos, float thresholdDb) noexcept;

    /** True when a level is known for `pos` (inside the ring, not newer than the data). */
    bool covers (std::int64_t pos) const noexcept;

    /** RMS level (dBFS) of the slot containing `pos`; kSilenceDb when not covered. */
    float levelDbAt (std::int64_t pos) const noexcept;

    /** Newest onset in [from, from + window] nearest to `from`; -1 when none. */
    std::int64_t nearestOnsetWithin (std::int64_t from, std::int64_t window) const noexcept;

    int onsetCount() const noexcept   { return numOnsets; }
    std::int64_t newestKnownPos() const noexcept   { return haveData ? (newestSlot + 1) * kSlot - 1 : -1; }

private:
    void closeSlot (std::int64_t slot, float thresholdDb) noexcept;

    double sr = 48000.0;
    int window = 240;                         ///< RMS window in samples
    int floorSlots = 60;
    std::int64_t refractory = 2400;
    std::vector<float> squares;               ///< circular, `window` entries
    int squareIdx = 0;
    double accum = 0.0;
    std::vector<float> levels;                ///< kSlots dB values
    std::int64_t nextPos = -1;                ///< expected position of the next sample
    std::int64_t newestSlot = -1;
    std::int64_t firstSlot = 0;
    bool haveData = false;
    bool armed = true;
    std::int64_t lastOnset = -1000000000;
    std::array<std::int64_t, kOnsets> onsets {};
    int numOnsets = 0, onsetHead = 0;
};

/** Gate verdict at output position `pos`: None, or ScGated. Inert (None) without data. */
RejectReason evaluateGate (const ScSettings&, std::int64_t pos) noexcept;

/** Hop-aligned evaluation target for a gap that would be evaluated at `outputPos`, or -1
    for "evaluate now". `availableAt(t)` returns the room (samples) a fill would have if
    it started at t. */
template <typename RoomFn>
std::int64_t chooseSnapTarget (const ScSettings& sc, std::int64_t outputPos, std::int64_t hop,
                               double sampleRate, std::int64_t lookaheadSamples,
                               std::int64_t minGapSamples, RoomFn&& availableAt) noexcept
{
    if (sc.tracker == nullptr || sc.snap01 <= 0.0f)
        return -1;

    const auto window = (std::int64_t) ((double) kSnapWindowMs * 0.001 * sampleRate * (double) sc.snap01);
    const auto bounded = lookaheadSamples < window ? lookaheadSamples : window;

    if (bounded < 1)
        return -1;

    const auto onset = sc.tracker->nearestOnsetWithin (outputPos, bounded);

    if (onset < 0)
        return -1;

    const auto target = ((onset + hop / 2) / hop) * hop;      // nearest hop boundary

    if (target <= outputPos)
        return -1;

    return availableAt (target) >= minGapSamples ? target : -1;
}

} // namespace gapfill::scdrive
