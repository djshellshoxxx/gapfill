#pragma once

// N-MUS Scale Lock (docs/dev/12 section 5): pure pitch-snapping maths.
//
// No JUCE, no engine state, no allocation: a ScaleSet is a 12-bit pitch-class
// mask relative to a root. Everything here is deterministic and safe on the
// audio thread.
//
// Contract
//  - snapInterval: a RELATIVE shift of `semis` semitones applied to a source
//    whose pitch class is `ctx.sourcePc` must land on a pitch class of the
//    scale. Result is the nearest such interval (ties: smaller |interval|,
//    then upward), shifts of 0 or any multiple of 12 are always allowed, and
//    the change never exceeds 6 semitones, so octaves are preserved.
//  - snapFrequency: an ABSOLUTE pitch is moved to the nearest scale degree
//    (root fixed, ties upward); the result is an exact equal-tempered note
//    (A4 = 440 Hz), i.e. within 1 cent of the target.
//  - An inactive ScaleSet (type Off) is the identity everywhere.

#include <cstdint>

namespace gapfill::scale
{

inline constexpr int kNumScaleTypes = 13;      ///< Off + 12 scales (choice indices of `scaleType`)
inline constexpr int kNumRoots = 12;

/** Index into the `scaleType` choice: 0 = Off, 1.. = kScaleNames. */
enum class ScaleType : std::uint8_t
{
    Off = 0, Major, NaturalMinor, Dorian, Phrygian, Lydian, Mixolydian,
    HarmonicMinor, MelodicMinor, MajorPentatonic, MinorPentatonic, Blues, WholeTone
};

/** Semitones above the root, ascending, for scale type 1..12; `count` entries. */
struct ScaleTable { int count; int degrees[7]; };

const ScaleTable& scaleTable (int type) noexcept;      ///< type 1..12, else the Off (empty) table
const char* scaleName (int type) noexcept;             ///< "Off", "Major", ...
const char* rootName (int root) noexcept;              ///< "C", "C#", ... "B"

/** A scale relative to its root. */
struct ScaleSet
{
    std::uint16_t mask = 0;     ///< bit k set: (root + k) mod 12 is a scale degree
    std::uint8_t  root = 0;     ///< 0..11 (C = 0)
    std::uint8_t  type = 0;     ///< ScaleType index

    bool active() const noexcept { return type != 0 && mask != 0; }

    /** True when the interval `k` semitones above the root is a degree. */
    bool hasDegree (int k) const noexcept { return ((mask >> (((k % 12) + 12) % 12)) & 1) != 0; }

    /** True when absolute pitch class `pc` (0 = C) belongs to the scale. */
    bool containsPitchClass (int pc) const noexcept { return hasDegree (pc - (int) root); }
};

/** Builds the set for root 0..11 and type 0..12 (out-of-range values are clamped; Off is inactive). */
ScaleSet makeScaleSet (int root, int type) noexcept;

/** The source's pitch class for a relative shift. Default = the scale root
    (the user states the song's tonic). `conf` is the confidence of an
    externally detected pitch class (F-X03 / F-X01); only values >= 0 are used. */
struct PitchContext
{
    int   sourcePc = -1;        ///< -1 = use the scale root
    float conf = 0.0f;
};

int snapInterval (int semis, const ScaleSet&, const PitchContext& = {}) noexcept;

/** Float cents version used by the Granular Cloud: rounds to semitones, snaps, returns cents. */
float snapCents (float cents, const ScaleSet&, const PitchContext& = {}) noexcept;

/** Nearest in-scale frequency (Hz); non-positive or non-finite input is returned unchanged. */
float snapFrequency (float hz, const ScaleSet&) noexcept;

/** The pitch ratio set used by Granular grains when the lock is on: 2^(k/12) for
    in-scale k whose ratio lies in [minRatio, maxRatio]; the ratio nearest to
    `ratio` (ties toward the smaller shift) is returned. */
float snapRatio (float ratio, const ScaleSet&, const PitchContext& = {},
                 float minRatio = 0.25f, float maxRatio = 2.0f) noexcept;

/** Parameter-derived settings. Off or amount 0 = neutral. */
struct ScaleSettings
{
    ScaleSet set;
    float amount01 = 1.0f;      ///< probability that a pitch decision is snapped

    bool active() const noexcept { return set.active() && amount01 > 0.0f; }
};

/** "D minor"-style label: root + scale name; empty when Off. */
const char* describeShort (const ScaleSet&, char* buffer, int bufferSize) noexcept;

/** Writes the active pitch-class names ("D E F G A A# C") into `buffer`; "" when Off. */
const char* describePitchClasses (const ScaleSet&, char* buffer, int bufferSize) noexcept;

} // namespace gapfill::scale
