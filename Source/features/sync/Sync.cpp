#include "Sync.h"

#include <algorithm>
#include <cmath>

namespace gapfill::sync
{

double TempoSource::resolve (bool hostValid, double hostBpm, int samples) noexcept
{
    if (hostValid && isPlausibleBpm (hostBpm))
    {
        lastKnownBpm.store (hostBpm, std::memory_order_relaxed);
        samplesSinceHost.store (0, std::memory_order_relaxed);
        return hostBpm;
    }

    const auto age = samplesSinceHost.load (std::memory_order_relaxed);
    samplesSinceHost.store (std::min<std::int64_t> (age + std::max (0, samples), 1000000000LL),
                            std::memory_order_relaxed);

    const auto last = lastKnownBpm.load (std::memory_order_relaxed);

    if (isPlausibleBpm (last))
        return last;

    return fallbackBpm.load (std::memory_order_relaxed);
}

void TempoSource::setFallbackBpm (double bpm) noexcept
{
    if (! std::isfinite (bpm))
        bpm = kDefaultFallbackBpm;

    fallbackBpm.store (std::clamp (bpm, kMinFallbackBpm, kMaxFallbackBpm), std::memory_order_relaxed);
}

bool TempoSource::hostTempoSeenRecently (double sampleRate, double seconds) const noexcept
{
    return (double) samplesSinceHost.load (std::memory_order_relaxed) < seconds * sampleRate;
}

void TempoSource::reset() noexcept
{
    lastKnownBpm.store (0.0, std::memory_order_relaxed);
    samplesSinceHost.store (1000000000LL, std::memory_order_relaxed);
}

//==============================================================================
namespace
{
    // Division indices (Parameters.h): 0-5 straight (1/1 .. 1/32), 6-8 triplets, 9-11 dotted.
    struct Family { int first, last; };

    Family familyOf (int div) noexcept
    {
        if (div >= 9) return { 9, 11 };
        if (div >= 6) return { 6, 8 };
        return { 0, 5 };
    }
}

ClampResult clampToDivision (float seconds, float lo, float hi, int divIndex) noexcept
{
    ClampResult r;
    r.seconds = seconds;
    r.division = divIndex;

    if (! std::isfinite (seconds) || seconds <= 0.0f)
    {
        r.seconds = lo;
        r.clamped = true;
        return r;
    }

    const auto fam = familyOf (divIndex);

    while (r.seconds > hi)
    {
        r.clamped = true;
        r.seconds *= 0.5f;

        if (r.division < fam.last)
            ++r.division;
    }

    while (r.seconds < lo)
    {
        r.clamped = true;
        r.seconds *= 2.0f;

        if (r.division > fam.first)
            --r.division;
    }

    // Defensive: guarantees the documented bounds even for hi < 2 * lo.
    r.seconds = std::clamp (r.seconds, lo, hi);
    return r;
}

float foldIntoRange (float hz, float lo, float hi) noexcept
{
    if (! std::isfinite (hz) || hz <= 0.0f)
        return lo;

    while (hz < lo) hz *= 2.0f;
    while (hz > hi) hz *= 0.5f;
    return std::clamp (hz, lo, hi);
}

float modSyncTremoloHz (float stutterSeconds) noexcept
{
    return foldIntoRange (1.0f / std::max (stutterSeconds, 1.0e-4f), 2.0f, 32.0f);
}

float modSyncPanHz (float delaySeconds) noexcept
{
    return foldIntoRange (1.0f / std::max (delaySeconds * 4.0f, 1.0e-4f), 0.25f, 8.0f);
}

double fillQuantBeats (int choiceIndex) noexcept
{
    switch (choiceIndex)
    {
        case 1:  return 1.0;
        case 2:  return 0.5;
        case 3:  return 0.25;
        case 4:  return 0.125;
        default: return 0.0;
    }
}

std::int64_t quantiseFillLength (std::int64_t length, double unitSamples,
                                 std::int64_t minLength, std::int64_t usable) noexcept
{
    if (! (unitSamples > 1.0) || length <= 0)
        return length;

    const auto q = (std::int64_t) (std::floor ((double) length / unitSamples) * unitSamples);

    if ((double) q < unitSamples || q < minLength || q > usable || q > length)
        return length;

    return q;
}

} // namespace gapfill::sync
