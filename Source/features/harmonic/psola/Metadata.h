#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

namespace gapfill::harmonic::psola
{
enum class Outcome : std::uint8_t { None = 0, Used = 1, UnvoicedFallback = 2, EpochFallback = 3 };

/** Optional recipe sidecar, never a FillRecipe flag. Missing/all-zero v0 means
    preserve legacy replay. V1 stores the actual final voice decision. f0Hz is
    the tracked source pitch (zero for UnvoicedFallback); clarity is0..1. */
struct DecisionMetadata
{
    std::uint8_t version = 0;
    Outcome outcome = Outcome::None;
    std::uint8_t intervals = 0;
    std::uint8_t voices = 0;
    float f0Hz = 0;
    float clarity = 0;
    bool operator== (const DecisionMetadata&) const noexcept = default;
};
static_assert (sizeof (DecisionMetadata) == 12 && std::is_trivially_copyable_v<DecisionMetadata>);
inline constexpr int kMetadataBytes = 12;
bool valid (const DecisionMetadata&) noexcept;
/** Writes exactly12 bytes, explicit little endian. Invalid input leaves the
    output untouched. The enclosing carrier decides whether absence is emitted. */
bool encode (const DecisionMetadata&, std::span<std::uint8_t>) noexcept;
/** Invalid/unknown input returns false and clears the optional sidecar. The
    caller preserves its independently validated16-byte base recipe. */
bool decode (std::span<const std::uint8_t>, DecisionMetadata&) noexcept;
} // namespace gapfill::harmonic::psola
