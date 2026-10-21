#pragma once

// F-S06 A/B compare with level matching (docs/dev/12-SPEC-BACKLOG-AND-NEW.md F-S06).
//
// The header A/B swaps two whole sounds, which are rarely equally loud, and a louder
// side wins every comparison. With "Match levels" on, each side measures the loudness of
// the WET CONTRIBUTION it produces and switching applies an output trim so the sides sound
// equally loud.
//
//   meter   100 Hz high-pass (HighPass2), 400 ms mean-square windows, windows below
//           -60 dBFS ignored (silence is not a measurement), 10 s EMA of the rest
//   trim    once BOTH sides have >= 3 s of data:  dB = 10 log10 (L_other / L_active),
//           clamped to +-6 dB, smoothed over 50 ms, shown as "+1.4 dB match"
//
// The trim is never saved, never an APVTS value, and ramps away when the switch goes off.
// Only the on/off switch is state (`<Perf>`).
//
// Threading: measure() / nextGain() run on the audio thread. setEnabled / setActiveSide
// are message-thread atomics. Nothing here allocates or locks.

#include "../../dsp/DspUtils.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>

namespace gapfill::abcompare
{

inline constexpr double kWindowSeconds   = 0.4;
inline constexpr double kEmaSeconds      = 10.0;
inline constexpr double kMinDataSeconds  = 3.0;
inline constexpr double kRampSeconds     = 0.05;
inline constexpr float  kGateMeanSquare  = 1.0e-6f;      ///< -60 dBFS
inline constexpr float  kMaxTrimDb       = 6.0f;
inline constexpr float  kHighPassHz      = 100.0f;

/** The pure trim rule, shared by the audio path and the tests. Returns 0 when either side is
    silent or has too little data (no trim is ever invented from nothing). */
float trimDbFor (float otherMeanSquare, double otherSeconds, float activeMeanSquare, double activeSeconds) noexcept;

/** "+1.4 dB match" (empty for no trim). */
juce::String trimText (float trimDb);

class MatchLevels
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;                           ///< forgets both sides and the trim

    //==========================================================================
    // Message thread
    void setEnabled (bool on) noexcept               { enabled.store (on, std::memory_order_relaxed); }
    bool isEnabled() const noexcept                  { return enabled.load (std::memory_order_relaxed); }

    /** Which header A/B side is sounding (0 = A, 1 = B). Pure bookkeeping: no undo entry, no parameter. */
    void setActiveSide (int side) noexcept           { activeSide.store (side != 0 ? 1 : 0, std::memory_order_relaxed); }
    int  getActiveSide() const noexcept              { return activeSide.load (std::memory_order_relaxed); }

    /** The trim currently requested, in dB (0 when none). */
    float getTrimDb() const noexcept                 { return trimDb.load (std::memory_order_relaxed); }

    float getSideMeanSquare (int side) const noexcept    { return sideMs[side != 0].load (std::memory_order_relaxed); }
    double getSideSeconds (int side) const noexcept      { return sideSecs[side != 0].load (std::memory_order_relaxed); }

    //==========================================================================
    // Audio thread

    /** Measures the wet bus of one chunk into the active side (only while enabled) and
        updates the trim target. */
    void measure (const float* left, const float* right, int numSamples) noexcept;

    /** Per-sample output trim (1.0 = none), smoothed over 50 ms. */
    float nextGain() noexcept;

private:
    struct Side
    {
        float ms = 0.0f;
        double seconds = 0.0;
    };

    void updateTarget() noexcept;

    double sr = 48000.0;
    int windowLength = 19200;
    float emaAlpha = 0.04f;

    HighPass2 hp[2];
    Side side[2];
    double windowSum = 0.0;
    int windowCount = 0;
    int measuredSide = 0;
    int skipWindows = 0;            ///< filter warm-up: the first window after enabling / a side switch is not measured
    bool wasEnabled = false;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain { 1.0f };

    std::atomic<bool> enabled { false };
    std::atomic<int> activeSide { 0 };
    std::atomic<float> trimDb { 0.0f };
    std::atomic<float> sideMs[2] { 0.0f, 0.0f };
    std::atomic<double> sideSecs[2] { 0.0, 0.0 };
};

} // namespace gapfill::abcompare
