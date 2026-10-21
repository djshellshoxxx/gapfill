#pragma once

// F-X03 Automatic key detection (docs/dev/features/F-X03): the pure estimator.
//
// Krumhansl-Kessler probe-tone profiles (Krumhansl, Cognitive Foundations of
// Musical Pitch, 1990), Pearson correlation against the leaky chroma
// accumulator for the 24 keys, a confidence that is low when two keys score
// alike (e.g. relative major/minor) and when little tonal material was heard,
// and a hysteresis that only changes the published key after three consecutive
// evaluations in which a new key leads by >= 0.05 correlation.

#include <array>
#include <atomic>
#include <cstdint>

namespace gapfill::keydetect
{

inline constexpr int kNumKeys = 24;               ///< index = tonic + 12 * mode (0 major, 1 minor)

/** The published estimate. Packed into one atomic word for the UI and the scheduler. */
struct KeyEstimate
{
    std::int8_t  tonic = -1;          ///< 0..11 (C = 0), -1 = none
    std::uint8_t mode = 0;            ///< 0 major, 1 minor
    std::uint8_t confidence = 0;      ///< 0..100
    bool         valid = false;

    int keyIndex() const noexcept { return tonic < 0 ? -1 : tonic + 12 * (int) mode; }
};

std::uint32_t pack (const KeyEstimate&) noexcept;
KeyEstimate   unpack (std::uint32_t) noexcept;

/** Result of one evaluation, before hysteresis. */
struct KeyDecision
{
    std::array<float, kNumKeys> r {};
    int best = 0, second = 1;
    float confidence = 0.0f;          ///< 0..1
    bool valid = false;
};

/** Profile value for key `index` at absolute pitch class `pc`. */
float profileValue (int keyIndex, int pc) noexcept;

float pearson (const float* a, const float* b, int n) noexcept;

/** Correlates the chroma vector (any scale) against the 24 keys. `tonalMass` is 0..1. */
KeyDecision evaluate (const float (&chroma)[12], float tonalMass, float minConfidence01) noexcept;

class KeyHysteresis
{
public:
    void reset() noexcept;

    /** Seeds the published key (warm start / hold). */
    void seed (const KeyEstimate&) noexcept;

    KeyEstimate update (const KeyDecision&) noexcept;
    KeyEstimate current() const noexcept { return published; }
    int changes() const noexcept { return keyChanges; }

private:
    KeyEstimate published;
    int pendingKey = -1, pendingCount = 0, invalidCount = 0;
    int keyChanges = 0;
};

} // namespace gapfill::keydetect
