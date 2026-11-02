#pragma once

#include <algorithm>
#include <cmath>

namespace gapfill::voiceTiming
{
inline constexpr double kMaximumCaptureSeconds = 0.32;

inline int maximumCaptureSamples (double sampleRate) noexcept
{
    return (int) std::ceil (sampleRate * kMaximumCaptureSeconds);
}

inline int sliceOffsetSamples (int captureLength, int sinceEventSamples, double sampleRate) noexcept
{
    return std::clamp (captureLength - sinceEventSamples - (int) (sampleRate * 0.004),
                       0, std::max (0, captureLength - 64));
}

inline int syncedScrambleSliceSamples (int usableSamples, float periodSeconds, double sampleRate) noexcept
{
    return std::clamp ((int) std::lround (periodSeconds * sampleRate), 48, std::max (48, usableSamples / 2));
}

/** Same capture and slice clamps used by FillVoice startup, for the Groove
    setup stage which must warp the actual period rather than an unclamped request. */
inline int syncedScrambleSliceForPlan (int plannedSamples, int sinceEventSamples,
                                      float periodSeconds, double sampleRate) noexcept
{
    const auto maximum = maximumCaptureSamples (sampleRate);
    const auto capture = std::clamp (std::min (std::max (32, plannedSamples), maximum), 64, maximum);
    const auto offset = sliceOffsetSamples (capture, sinceEventSamples, sampleRate);
    return syncedScrambleSliceSamples (std::max (128, capture - offset), periodSeconds, sampleRate);
}
} // namespace gapfill::voiceTiming
