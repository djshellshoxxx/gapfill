#include "RecipeExtras.h"
#include "../features/tapedamage/TapeMetaCodec.h"

#include <algorithm>
#include <array>

namespace gapfill
{
bool isValid (const RecipeExtras& extras) noexcept
{
    return harmonic::psola::valid (extras.harmonic) && tapedamage::isValidTapeMeta (extras.tape);
}

bool encodeRecipeExtras (const RecipeExtras& extras, std::span<std::uint8_t> destination) noexcept
{
    if (destination.size() != kRecipeExtrasBytes || ! isValid (extras))
        return false;
    std::array<std::uint8_t, kRecipeExtrasBytes> bytes {};
    bytes[0] = kRecipeExtrasSchema;
    bytes[1] = (std::uint8_t) ((extras.harmonic.version != 0 ? 1 : 0) | (extras.tape.version != 0 ? 2 : 0));
    auto payload = std::span (bytes);
    if (! harmonic::psola::encode (extras.harmonic, payload.subspan (4, harmonic::psola::kMetadataBytes))
        || ! tapedamage::encodeTapeMeta (extras.tape, payload.subspan (16, tapedamage::kTapeMetaBytes)))
        return false;
    std::copy (bytes.begin(), bytes.end(), destination.begin());
    return true;
}

bool decodeRecipeExtras (std::span<const std::uint8_t> bytes, RecipeExtras& destination) noexcept
{
    if (bytes.size() != kRecipeExtrasBytes || bytes[0] != kRecipeExtrasSchema
        || bytes[1] > 3 || bytes[2] != 0 || bytes[3] != 0)
        return false;
    RecipeExtras extras;
    if (! harmonic::psola::decode (bytes.subspan (4, harmonic::psola::kMetadataBytes), extras.harmonic)
        || ! tapedamage::decodeTapeMeta (bytes.subspan (16, tapedamage::kTapeMetaBytes), extras.tape))
        return false;
    const auto presence = (extras.harmonic.version != 0 ? 1 : 0) | (extras.tape.version != 0 ? 2 : 0);
    if (bytes[1] != presence)
        return false;
    destination = extras;
    return true;
}
} // namespace gapfill
