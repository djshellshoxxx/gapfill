#pragma once

// Shared visualisation-only cadence. Keep the released producer arithmetic;
// history allocation uses the same period without changing audio scheduling.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gapfill::visualisation
{
inline constexpr int analysisHopSamples = 32;
inline constexpr double targetFramesPerSecond = 700.0;

inline int decimationForRate (double sampleRate) noexcept
{
    const auto rate = std::isfinite (sampleRate) ? std::clamp (sampleRate, 8000.0, (double) UINT32_MAX) : 48000.0;
    return std::max (1, (int) std::round (rate / (double) analysisHopSamples / targetFramesPerSecond));
}

inline std::uint32_t periodForRate (double sampleRate) noexcept
{
    return (std::uint32_t) (analysisHopSamples * decimationForRate (sampleRate));
}
} // namespace gapfill::visualisation
