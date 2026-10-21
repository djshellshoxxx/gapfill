#pragma once

// T-01 level-meter arithmetic (docs/dev/features/T-01): the dB mapping, the 1.5 s peak hold, the 20 dB/s fall
// and the gradient, extracted from LevelMeter so they can be tested with an injected clock. Header-only, no
// component, no timer. LevelMeter delegates here and behaves exactly as before (theme.md: gradient, 1.5 s hold,
// 20 dB/s fall).

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>

#include "LookAndFeel.h"

namespace gapfill::meter
{

inline constexpr float kFloorDb = -60.0f;
inline constexpr float kCeilDb = 6.0f;                    ///< top of the scale (normalised 1.0)
inline constexpr juce::uint32 kHoldMs = 1500;
inline constexpr float kFallDbPerSecond = 20.0f;
inline constexpr float kTimerHz = 30.0f;                  ///< rate of LevelMeter's animation timer

/** Linear gain -> dB with the silence floor the meter uses (-100 dB for 0). */
inline float toDb (float gain) noexcept
{
    return juce::Decibels::gainToDecibels (juce::jmax (1.0e-5f, gain));
}

/** dB -> 0..1 position on the meter (monotonic; kFloorDb -> 0, kCeilDb -> 1). */
inline float normalised (float db) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (kCeilDb - kFloorDb));
}

/** The one gradient across the whole track: teal -> orange (0.62) -> yellow (0.86) -> red. */
inline juce::ColourGradient makeGradient (float x, float bottomY, float topY)
{
    juce::ColourGradient grad (colours::secondary, x, bottomY, colours::clip, x, topY, false);
    grad.addColour (0.62, colours::primary);
    grad.addColour (0.86, colours::warning);
    return grad;
}

/** The colour the track shows at a normalised level (0 = bottom, 1 = top). */
inline juce::Colour gradientAt (float norm)
{
    return makeGradient (0.0f, 1.0f, 0.0f).getColourAtPosition ((double) juce::jlimit (0.0f, 1.0f, norm));
}

/** Peak line: holds for kHoldMs after the last peak that reached it, then falls at kFallDbPerSecond. */
struct PeakHold
{
    enum class Tick { Held, Falling, Settled };

    float peakDb = kFloorDb;
    juce::uint32 holdUntilMs = 0;

    /** A new level in dB. A level at or above the held peak replaces it and restarts the hold
        (returns true: the peak now has to be animated back down). A lower level changes nothing. */
    bool push (float db, juce::uint32 nowMs) noexcept
    {
        if (db < peakDb)
            return false;

        peakDb = db;
        holdUntilMs = nowMs + kHoldMs;
        return true;
    }

    /** One animation step of `dtSeconds` (default: one timer period). Held = nothing moved, Falling = peak lowered, Settled = reached the floor
        (the caller stops its timer). */
    Tick tick (juce::uint32 nowMs, float dtSeconds = 0.0f) noexcept
    {
        if (nowMs < holdUntilMs)
            return Tick::Held;

        // dtSeconds <= 0 means "one timer period", computed as it always was (20 dB / 30).
        peakDb -= dtSeconds > 0.0f ? kFallDbPerSecond * dtSeconds : kFallDbPerSecond / kTimerHz;

        if (peakDb <= kFloorDb)
        {
            peakDb = kFloorDb;
            return Tick::Settled;
        }

        return Tick::Falling;
    }

    void reset() noexcept { peakDb = kFloorDb; }
};

} // namespace gapfill::meter
