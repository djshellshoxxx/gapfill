#include "Metadata.h"
#include <bit>
#include <cmath>

namespace gapfill::harmonic::psola
{
bool valid (const DecisionMetadata& m) noexcept
{
    if (m.version == 0)
        return m.outcome == Outcome::None && m.intervals == 0 && m.voices == 0
            && std::bit_cast<std::uint32_t> (m.f0Hz) == 0 && std::bit_cast<std::uint32_t> (m.clarity) == 0;
    if (m.version != 1 || m.intervals > 5 || m.voices < 1 || m.voices > 4
        || ! std::isfinite (m.f0Hz) || ! std::isfinite (m.clarity) || m.clarity < 0 || m.clarity > 1)
        return false;
    if (m.outcome == Outcome::UnvoicedFallback) return m.f0Hz == 0;
    if (m.outcome == Outcome::Used || m.outcome == Outcome::EpochFallback)
        return m.f0Hz >= 50 && m.f0Hz <= 1000 && m.clarity >= 0.8f;
    return false;
}

bool applicableToAlgorithm (const DecisionMetadata& value, std::uint8_t algorithm) noexcept
{
    return valid (value) && (value.version == 0 || algorithm == 3);
}

bool encode (const DecisionMetadata& m, std::span<std::uint8_t> bytes) noexcept
{
    if (bytes.size() != kMetadataBytes || ! valid (m)) return false;
    bytes[0] = m.version; bytes[1] = (std::uint8_t) m.outcome;
    bytes[2] = m.intervals; bytes[3] = m.voices;
    const std::uint32_t words[] { std::bit_cast<std::uint32_t> (m.f0Hz), std::bit_cast<std::uint32_t> (m.clarity) };
    for (int word = 0; word < 2; ++word)
        for (int i = 0; i < 4; ++i)
            bytes[(std::size_t) (4 + word * 4 + i)] = (std::uint8_t) (words[word] >> (i * 8));
    return true;
}

bool decode (std::span<const std::uint8_t> bytes, DecisionMetadata& result) noexcept
{
    result = {};
    if (bytes.size() != kMetadataBytes) return false;
    DecisionMetadata value;
    value.version = bytes[0]; value.outcome = (Outcome) bytes[1];
    value.intervals = bytes[2]; value.voices = bytes[3];
    std::uint32_t words[2] {};
    for (int word = 0; word < 2; ++word)
        for (int i = 0; i < 4; ++i)
            words[word] |= (std::uint32_t) bytes[(std::size_t) (4 + word * 4 + i)] << (i * 8);
    value.f0Hz = std::bit_cast<float> (words[0]); value.clarity = std::bit_cast<float> (words[1]);
    if (! valid (value)) return false;
    result = value;
    return true;
}
} // namespace gapfill::harmonic::psola
