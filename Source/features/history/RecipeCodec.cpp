#include "RecipeCodec.h"

#include <array>

namespace gapfill::recipecodec
{

std::uint8_t checksum (const std::uint8_t (&bytes)[FillRecipe::serialisedBytes]) noexcept
{
    unsigned acc = 0xA5u;

    for (auto b : bytes)
        acc = (((acc << 1) | (acc >> 7)) & 0xFFu) ^ b;

    return (std::uint8_t) (acc & 0xFFu);
}

juce::String encode (const FillRecipe& r)
{
    std::uint8_t raw[FillRecipe::serialisedBytes + 1];
    std::uint8_t body[FillRecipe::serialisedBytes];
    toBytes (r, body);
    std::memcpy (raw, body, sizeof (body));
    raw[FillRecipe::serialisedBytes] = checksum (body);
    return juce::Base64::toBase64 (raw, sizeof (raw));
}

bool decode (const juce::String& text, FillRecipe& out)
{
    // 17 bytes encode to 24 base64 characters; refuse anything much longer before decoding.
    if (text.length() != 24)
        return false;

    juce::MemoryOutputStream stream;

    if (! juce::Base64::convertFromBase64 (stream, text) || stream.getDataSize() != FillRecipe::serialisedBytes + 1)
        return false;

    std::uint8_t body[FillRecipe::serialisedBytes];
    std::memcpy (body, stream.getData(), sizeof (body));
    const auto sum = static_cast<const std::uint8_t*> (stream.getData())[FillRecipe::serialisedBytes];

    if (sum != checksum (body))
        return false;

    const auto recipe = fromBytes (body);

    if (! isValid (recipe))
        return false;

    out = recipe;
    return true;
}

namespace
{
    std::uint8_t extrasChecksum (std::span<const std::uint8_t> bytes) noexcept
    {
        unsigned value = 0xA5u;
        for (const auto byte : bytes)
            value = (((value << 1) | (value >> 7)) & 0xFFu) ^ byte;
        return (std::uint8_t) value;
    }
}

juce::String encodeExtras (const RecipeExtras& extras)
{
    if (! hasRecipeExtras (extras))
        return {};
    std::array<std::uint8_t, kRecipeExtrasBytes + 1> bytes {};
    if (! encodeRecipeExtras (extras, std::span (bytes).first (kRecipeExtrasBytes)))
        return {};
    bytes.back() = extrasChecksum (std::span (bytes).first (kRecipeExtrasBytes));
    return juce::Base64::toBase64 (bytes.data(), bytes.size());
}

bool decodeExtras (const juce::String& text, RecipeExtras& out)
{
    if (text.length() != 60)
        return false;
    juce::MemoryOutputStream stream;
    if (! juce::Base64::convertFromBase64 (stream, text) || stream.getDataSize() != kRecipeExtrasBytes + 1)
        return false;
    const auto bytes = std::span (static_cast<const std::uint8_t*> (stream.getData()), stream.getDataSize());
    if (bytes.back() != extrasChecksum (bytes.first (kRecipeExtrasBytes)))
        return false;
    return decodeRecipeExtras (bytes.first (kRecipeExtrasBytes), out);
}

} // namespace gapfill::recipecodec
