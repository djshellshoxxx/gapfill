#include "TapeMetaCodec.h"

#include <algorithm>
#include <cmath>

namespace gapfill::tapedamage
{
bool encodeTapeMeta (const TapeMeta& meta, std::span<std::uint8_t> output) noexcept
{
    if (output.size() != kTapeMetaBytes || ! isValidTapeMeta (meta)) return false;
    std::array<std::uint8_t, kTapeMetaBytes> bytes {};
    bytes[0] = meta.version;
    bytes[1] = meta.chosen;
    for (std::size_t i = 0; i < 8; ++i)
        bytes[2 + i] = (std::uint8_t) (meta.subseed >> (8u * i));
    for (std::size_t i = 0; i < NumValues; ++i)
    {
        bytes[10 + 2 * i] = (std::uint8_t) (meta.values[i] & 0xffu);
        bytes[11 + 2 * i] = (std::uint8_t) (meta.values[i] >> 8u);
    }
    std::copy (bytes.begin(), bytes.end(), output.begin());
    return true;
}

bool decodeTapeMeta (std::span<const std::uint8_t> input, TapeMeta& output) noexcept
{
    if (input.size() != kTapeMetaBytes) return false;
    TapeMeta candidate;
    candidate.version = input[0];
    candidate.chosen = input[1];
    for (std::size_t i = 0; i < 8; ++i)
        candidate.subseed |= (std::uint64_t) input[2 + i] << (8u * i);
    for (std::size_t i = 0; i < NumValues; ++i)
        candidate.values[i] = (std::uint16_t) ((std::uint16_t) input[10 + 2 * i]
                                              | ((std::uint16_t) input[11 + 2 * i] << 8u));
    if (! isValidTapeMeta (candidate)) return false;
    output = candidate;
    return true;
}

TapeMeta latchTapeSettings (const fx::TapeExtraSettings& settings, std::uint64_t subseed) noexcept
{
    if (! std::isfinite (settings.amount01) || settings.amount01 <= 0.0f) return {};
    const fx::TapeExtraSettings defaults;
    const float values[NumValues] { settings.amount01, settings.wow01, settings.flutter01, settings.sat01,
                                  settings.dropDensity01, settings.dropDepth01, settings.splice01,
                                  settings.instab01, settings.mix01 };
    const float fallback[NumValues] { defaults.amount01, defaults.wow01, defaults.flutter01, defaults.sat01,
                                    defaults.dropDensity01, defaults.dropDepth01, defaults.splice01,
                                    defaults.instab01, defaults.mix01 };
    TapeMeta meta;
    meta.version = 1;
    meta.subseed = subseed;
    for (std::size_t i = 0; i < NumValues; ++i)
    {
        const auto value = std::clamp (std::isfinite (values[i]) ? values[i] : fallback[i], 0.0f, 1.0f);
        meta.values[i] = (std::uint16_t) std::lround (value * kTapeValueMax);
    }
    return meta.values[Amount] == 0 ? TapeMeta {} : meta;
}

fx::TapeExtraSettings settingsFromTapeMeta (const TapeMeta& meta) noexcept
{
    if (meta.version != 1 || ! isValidTapeMeta (meta)) return {};
    const auto value = [&] (TapeValueIndex index) { return (float) meta.values[(std::size_t) index] / kTapeValueMax; };
    return { value (Amount), value (Wow), value (Flutter), value (Saturation), value (DropDensity),
             value (DropDepth), value (Splice), value (Instability), value (Mix) };
}
} // namespace gapfill::tapedamage
