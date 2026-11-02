#pragma once

#include "../features/harmonic/psola/Metadata.h"
#include "../features/tapedamage/TapeMeta.h"

#include <span>
#include <type_traits>

namespace gapfill
{

/** Optional sound decisions alongside the permanent 16-byte FillRecipe.
    No pointers or runtime state. An absent feature always replays its legacy
    path; current controls must not invent new detailed decisions on replay. */
struct RecipeExtras
{
    tapedamage::TapeMeta tape;
    harmonic::psola::DecisionMetadata harmonic;
    bool operator== (const RecipeExtras&) const noexcept = default;
};

static_assert (sizeof (RecipeExtras) == 48);
static_assert (std::is_trivially_copyable_v<RecipeExtras> && std::is_standard_layout_v<RecipeExtras>);

inline constexpr int kRecipeExtrasBytes = 44;
inline constexpr std::uint8_t kRecipeExtrasSchema = 1;

bool isValid (const RecipeExtras&) noexcept;
/** Return a copy with inapplicable Harmonic decisions removed. The base recipe
    and independent Tape decision remain untouched. */
RecipeExtras normalizeRecipeExtrasForAlgorithm (std::uint8_t algorithm, RecipeExtras) noexcept;
/** Validate first when accepting untrusted metadata. Empty metadata is omitted
    from legacy files, preserving their exact bytes. */
inline bool hasRecipeExtras (const RecipeExtras& extras) noexcept
{
    return extras.harmonic.version != 0 || extras.tape.version != 0;
}

/** Fixed canonical envelope: schema1, presence mask, two zero bytes,
    harmonic12 bytes, Tape28 bytes. Memory padding is never serialized.
    Failed encoding/decoding leaves the destination unchanged. */
bool encodeRecipeExtras (const RecipeExtras&, std::span<std::uint8_t>) noexcept;
bool decodeRecipeExtras (std::span<const std::uint8_t>, RecipeExtras&) noexcept;

} // namespace gapfill
