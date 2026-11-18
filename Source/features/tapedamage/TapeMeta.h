#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

namespace gapfill::tapedamage
{
enum TapeValueIndex { Amount, Wow, Flutter, Saturation, DropDensity, DropDepth, Splice, Instability, Mix, NumValues };
inline constexpr int kTapeMetaBytes = 28;
inline constexpr std::uint16_t kTapeValueMax = 10000;

/** Recipe sidecar: version0 absent; version1 is a recorded decision, including
    chosen0. Values are 0.01-percent units, matching the nine fm20_* parameters.
    Memory layout is intentionally separate from canonical LE codec order. */
struct TapeMeta
{
    std::uint64_t subseed = 0;
    std::array<std::uint16_t, NumValues> values {};
    std::uint8_t version = 0;
    std::uint8_t chosen = 0;

    bool operator== (const TapeMeta&) const noexcept = default;
};

static_assert (sizeof (TapeMeta) == 32);
static_assert (std::is_trivially_copyable_v<TapeMeta>);

constexpr bool isValidTapeMeta (const TapeMeta& meta) noexcept
{
    if (meta.version == 0)
    {
        if (meta.subseed != 0 || meta.chosen != 0) return false;
        for (const auto q : meta.values) if (q != 0) return false;
        return true;
    }
    if (meta.version != 1 || meta.chosen > 1) return false;
    for (const auto q : meta.values) if (q > kTapeValueMax) return false;
    return meta.chosen == 0 || (meta.values[Amount] > 0 && meta.values[Mix] > 0);
}
} // namespace gapfill::tapedamage
