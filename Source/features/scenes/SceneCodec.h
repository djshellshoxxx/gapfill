#pragma once

// Scenes v2 wire records: unsigned 16-bit host index followed by IEEE float32,
// both little endian. v1 records used native byte order; their reader remains
// separate so the portable format never depends on the compiler's ABI.

#include <bit>
#include <cstdint>
#include <limits>

namespace gapfill::scenes::codec
{

static_assert (sizeof (float) == 4 && std::numeric_limits<float>::is_iec559);
inline constexpr int kRecordBytes = 6;

inline void writeLittleEndian (std::uint8_t* out, std::uint16_t index, float value) noexcept
{
    out[0] = (std::uint8_t) index;
    out[1] = (std::uint8_t) (index >> 8);
    const auto bits = std::bit_cast<std::uint32_t> (value);

    for (int byte = 0; byte < 4; ++byte)
        out[byte + 2] = (std::uint8_t) (bits >> (8 * byte));
}

inline void readLittleEndian (const std::uint8_t* data, std::uint16_t& index, float& value) noexcept
{
    index = (std::uint16_t) ((std::uint16_t) data[0] | ((std::uint16_t) data[1] << 8));
    std::uint32_t bits = 0;

    for (int byte = 0; byte < 4; ++byte)
        bits |= (std::uint32_t) data[byte + 2] << (8 * byte);

    value = std::bit_cast<float> (bits);
}

} // namespace gapfill::scenes::codec
