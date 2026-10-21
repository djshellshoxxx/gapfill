#include "MatchLevels.h"

namespace gapfill::abcompare
{

float trimDbFor (float otherMeanSquare, double otherSeconds, float activeMeanSquare, double activeSeconds) noexcept
{
    if (otherSeconds < kMinDataSeconds || activeSeconds < kMinDataSeconds)
        return 0.0f;

    if (! (otherMeanSquare > 0.0f) || ! (activeMeanSquare > 0.0f))
        return 0.0f;

    const auto db = 10.0f * std::log10 (otherMeanSquare / activeMeanSquare);

    if (! std::isfinite (db))
        return 0.0f;

    return juce::jlimit (-kMaxTrimDb, kMaxTrimDb, db);
}

juce::String trimText (float trimDb)
{
    if (std::abs (trimDb) < 0.05f)
        return {};

    return juce::String::formatted ("%+.1f dB match", (double) trimDb);
}

void MatchLevels::prepare (double sampleRate)
{
    sr = juce::jmax (8000.0, sampleRate);
    windowLength = juce::jmax (1, (int) std::lround (kWindowSeconds * sr));
    emaAlpha = (float) (kWindowSeconds / kEmaSeconds);

    for (auto& f : hp)
        f.set (sr, kHighPassHz);

    gain.reset (sr, kRampSeconds);
    reset();
}

void MatchLevels::reset() noexcept
{
    for (auto& f : hp)
        f.reset();

    side[0] = side[1] = {};
    windowSum = 0.0;
    windowCount = 0;
    skipWindows = 0;
    measuredSide = getActiveSide();
    wasEnabled = false;

    gain.setCurrentAndTargetValue (1.0f);
    trimDb.store (0.0f, std::memory_order_relaxed);

    for (int i = 0; i < 2; ++i)
    {
        sideMs[i].store (0.0f, std::memory_order_relaxed);
        sideSecs[i].store (0.0, std::memory_order_relaxed);
    }
}

void MatchLevels::measure (const float* left, const float* right, int numSamples) noexcept
{
    const bool on = enabled.load (std::memory_order_relaxed);

    if (! on)
    {
        if (wasEnabled)
        {
            wasEnabled = false;
            windowSum = 0.0;
            windowCount = 0;
        }

        // Ramp the trim away; a switch that was never on leaves the gain at exactly 1.0.
        gain.setTargetValue (1.0f);
        trimDb.store (0.0f, std::memory_order_relaxed);
        return;
    }

    if (! wasEnabled)
        skipWindows = 1;

    wasEnabled = true;
    const auto current = getActiveSide();

    if (current != measuredSide)
    {
        measuredSide = current;                 // a window belongs to one side: drop the partial one
        windowSum = 0.0;
        windowCount = 0;
        skipWindows = 1;                        // and let the high-pass settle on the new material
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto l = hp[0].process (left[i]);
        const auto r = hp[1].process (right != nullptr ? right[i] : left[i]);
        windowSum += (double) l * (double) l + (double) r * (double) r;

        if (++windowCount >= windowLength)
        {
            const auto ms = (float) (windowSum / (2.0 * (double) windowCount));
            windowSum = 0.0;
            windowCount = 0;

            if (skipWindows > 0)
            {
                --skipWindows;
                continue;
            }

            // Silence is not a measurement: windows below -60 dBFS never enter the average.
            if (std::isfinite (ms) && ms >= kGateMeanSquare)
            {
                auto& s = side[measuredSide];
                s.ms = s.seconds <= 0.0 ? ms : s.ms + emaAlpha * (ms - s.ms);
                s.seconds += kWindowSeconds;

                sideMs[measuredSide].store (s.ms, std::memory_order_relaxed);
                sideSecs[measuredSide].store (s.seconds, std::memory_order_relaxed);
            }
        }
    }

    updateTarget();
}

void MatchLevels::updateTarget() noexcept
{
    const auto active = measuredSide;
    const auto other = 1 - active;
    const auto db = trimDbFor (side[other].ms, side[other].seconds, side[active].ms, side[active].seconds);

    trimDb.store (db, std::memory_order_relaxed);
    gain.setTargetValue (db == 0.0f ? 1.0f : std::pow (10.0f, db / 20.0f));
}

float MatchLevels::nextGain() noexcept
{
    return gain.getNextValue();
}

} // namespace gapfill::abcompare
