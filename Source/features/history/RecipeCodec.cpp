#include "RecipeCodec.h"

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

} // namespace gapfill::recipecodec
