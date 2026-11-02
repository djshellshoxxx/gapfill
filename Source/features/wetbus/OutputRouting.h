#pragma once

// F-L12 routing coefficients. Main mode reuses the processor's established
// 20 ms wet-gain smoother; only the independent Wet-bus trim owns a new ramp.
// No IO, allocation, locks, GUI or persistent state. Audio-thread owned.

#include "../wetout/WetOut.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace gapfill::wetbus
{

inline constexpr int dryAndWet = 0;
inline constexpr int dryOnly = 1;
inline constexpr float minLevelDb = -24.0f, maxLevelDb = 12.0f;

/** Feed the existing dryGain/wetGain smoothers. The old helper supplies all
    neutral targets verbatim, including wetSolo's precedence over Dry only.
    A disabled Wet bus can never remove fills from the main output. */
inline wetout::MixTargets mainMixTargets (bool busEnabled, int mainMode, bool wetSolo,
                                         float dry01, float wet01) noexcept
{
    auto targets = wetout::mixTargets (wetSolo, dry01, wet01);
    if (busEnabled && mainMode == dryOnly && ! wetSolo)
        targets.wet = 0.0f;
    return targets;
}

/** The mono Wet output is the half-sum of the post-protection stereo wet tap. */
inline float monoSample (float left, float right) noexcept { return 0.5f * (left + right); }

class LevelSmoother
{
public:
    /** Prepare/reset while audio is stopped. The initial level is already
        settled, preserving the old unity bus when the parameter is absent. */
    void prepare (double sampleRate, float initialDb = 0.0f) noexcept
    {
        const auto rate = std::isfinite (sampleRate) ? juce::jlimit (8000.0, 768000.0, sampleRate) : 48000.0;
        gain.reset (rate, 0.020);
        targetDb = boundedDb (initialDb);
        gain.setCurrentAndTargetValue (gainForDb (targetDb));
    }

    /** Audio thread. Repeated targets do not restart the ramp or recompute pow. */
    void setTargetDecibels (float db) noexcept
    {
        db = boundedDb (db);
        if (db == targetDb)
            return;
        targetDb = db;
        gain.setTargetValue (gainForDb (db));
    }

    void reset() noexcept { gain.setCurrentAndTargetValue (gain.getTargetValue()); }
    float nextGain() noexcept { return gain.getNextValue(); }
    float currentGain() const noexcept { return gain.getCurrentValue(); }
    float targetGain() const noexcept { return gain.getTargetValue(); }
    bool isSmoothing() const noexcept { return gain.isSmoothing(); }
    /** Callers skip the new multiplication entirely for the old unity path. */
    bool isNeutral() const noexcept { return ! isSmoothing() && currentGain() == 1.0f; }

private:
    static float boundedDb (float db) noexcept
    {
        return std::isfinite (db) ? juce::jlimit (minLevelDb, maxLevelDb, db) : 0.0f;
    }
    static float gainForDb (float db) noexcept { return db == 0.0f ? 1.0f : juce::Decibels::decibelsToGain (db); }

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain { 1.0f };
    float targetDb = 0.0f;
};

} // namespace gapfill::wetbus
