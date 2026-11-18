#pragma once

#include "../../dsp/DspUtils.h"
#include "../Salts.h"

namespace gapfill::capture
{
/** The zero/default branch returns before a draw or anchor arithmetic. The
    selected slice still fits inside the actual readable captured window. */
inline std::int64_t scannedAnchor (std::int64_t anchor, const SourceHistory& history,
    bool held, float scan01, int windowSamples, int maximumSliceSamples, std::uint64_t key) noexcept
{
    if (! held || ! std::isfinite (scan01) || scan01 <= 0.0f)
        return anchor;
    const auto end = history.getWritePosition();
    const auto oldest = juce::jmax ((std::int64_t) 0, history.getReadFloor(),
        end - (std::int64_t) history.getSize() + 2, end - (std::int64_t) windowSamples);
    const auto minimum = oldest + (std::int64_t) maximumSliceSamples;
    if (anchor <= minimum) return anchor;
    const auto range = juce::jmin (anchor - minimum,
        (std::int64_t) juce::jmax (0, windowSamples - maximumSliceSamples));
    if (range <= 0) return anchor;
    Rng sub;
    sub.seed (hashCombine (key, kSalt_Capture));
    const auto back = (std::int64_t) ((double) sub.nextFloat() * (double) juce::jlimit (0.0f, 1.0f, scan01)
                                     * (double) range);
    return juce::jmax (minimum, anchor - back);
}
} // namespace gapfill::capture
