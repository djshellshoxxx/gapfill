#pragma once
#include "Settings.h"
#include <array>

namespace gapfill::harmonic::psola
{
struct RatioSet
{
    std::array<double, kMaxVoices> values {};
    int count = 0;
};
/** First requested entries of the canonical interval table. Scale uses absolute
    pitch-class bits and falls back to OpenFifths without a key. Key snap is
    continuous in cents; zero preserves exact just ratios. No RNG is consumed. */
RatioSet intervalRatios (const Settings&, float sourceF0Hz, const Scale&) noexcept;
/** Adapter for the existing ScaleSet's root-relative mask. Invalid/no-key
    input returns rootPc=-1; no framework/ScaleSet dependency enters the core. */
constexpr Scale absoluteScale (int rootPc, std::uint16_t relativeMask) noexcept
{
    if (rootPc < 0 || rootPc > 11 || relativeMask == 0 || (relativeMask & 0xf000u) != 0) return {};
    const auto bits = (std::uint32_t) relativeMask;
    return { (std::uint16_t) (((bits << rootPc) | (bits >> (12 - rootPc))) & 0x0fffu), rootPc };
}
} // namespace gapfill::harmonic::psola
