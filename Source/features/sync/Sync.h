#pragma once

// E-2 Tempo-sync audit and fixes (docs/dev/03-SPEC-ESSENTIALS.md E-2).
//
// Pure helpers plus one small thread-safe object (TempoSource). No JUCE UI, no
// allocation; every function is safe on the audio thread.
//
//   TempoSource        remembers the last host tempo; fallback tempo for hosts
//                      (and Standalone) that report none.
//   clampToDivision    musical clamping: when a synced time leaves its limits the
//                      DIVISION is halved/doubled instead of the milliseconds
//                      being clipped, so the audio is always a power-of-two
//                      related division of the knob setting.
//   foldIntoRange      octave folding for Sync Mods rates.
//   quantiseFillLength fill-length grid (never lengthens, never exceeds usable).

#include <atomic>
#include <cstdint>

namespace gapfill::sync
{

/** Valid range of the fallback tempo (Options: "Tempo when host provides none"). */
inline constexpr double kMinFallbackBpm = 40.0;
inline constexpr double kMaxFallbackBpm = 300.0;
inline constexpr double kDefaultFallbackBpm = 120.0;

/** Hosts may report any positive number; only this window is trusted as a tempo. */
inline bool isPlausibleBpm (double bpm) noexcept
{
    return bpm > 1.0 && bpm < 1000.0;
}

/** Tempo with memory (E-2 change 1). Thread-safe: the audio thread calls
    resolve(); the message thread edits the fallback. */
class TempoSource
{
public:
    /** Returns the tempo to use for tempo-synced lengths: the host's when valid
        (and remembers it), else the last known host tempo, else the fallback.
        Never returns 0, NaN or a negative value. `samples` is the block length,
        used to age "recently seen". */
    double resolve (bool hostValid, double hostBpm, int samples = 0) noexcept;

    double getFallbackBpm() const noexcept   { return fallbackBpm.load (std::memory_order_relaxed); }
    void   setFallbackBpm (double bpm) noexcept;
    double getLastKnownBpm() const noexcept  { return lastKnownBpm.load (std::memory_order_relaxed); }  ///< 0 = never seen

    /** True when the host reported a tempo within the last `seconds`. */
    bool hostTempoSeenRecently (double sampleRate, double seconds = 5.0) const noexcept;

    /** Forgets everything but the fallback (new session / sample-rate change). */
    void reset() noexcept;

private:
    std::atomic<double> lastKnownBpm { 0.0 };
    std::atomic<double> fallbackBpm { kDefaultFallbackBpm };
    std::atomic<std::int64_t> samplesSinceHost { 1000000000LL };
};

/** Result of a musical clamp. */
struct ClampResult
{
    float seconds = 0.0f;
    int   division = 0;       ///< the division actually used
    bool  clamped = false;
};

/** E-2 change 2. `seconds` = divisionInBeats(divIndex) * beatSeconds. If it is
    above `hi` the division is halved (staying inside its straight / triplet /
    dotted family, then plain halving) until it fits; if below `lo` it is
    doubled. The result is always inside [lo, hi] (requires hi >= 2 * lo). */
ClampResult clampToDivision (float seconds, float lo, float hi, int divIndex) noexcept;

/** Folds a frequency by octaves into [lo, hi] (hi >= 2 * lo). */
float foldIntoRange (float hz, float lo, float hi) noexcept;

/** Sync Mods: Tremolo rate in Hz derived from the stutter time (E-2 change 3). */
float modSyncTremoloHz (float stutterSeconds) noexcept;
/** Sync Mods: Pan Motion rate in Hz derived from the delay time. */
float modSyncPanHz (float delaySeconds) noexcept;

/** Length of the fillQuant choice (1..4 = 1/4, 1/8, 1/16, 1/32; 0 = Off) in beats; 0 = Off. */
double fillQuantBeats (int choiceIndex) noexcept;

/** E-2 change 4: floor to a whole number of `unitSamples`. Returns `length`
    unchanged when the grid is off (unit <= 1), when the result would be shorter
    than one unit or than `minLength`, or would exceed `usable`. */
std::int64_t quantiseFillLength (std::int64_t length, double unitSamples,
                                 std::int64_t minLength, std::int64_t usable) noexcept;

} // namespace gapfill::sync
