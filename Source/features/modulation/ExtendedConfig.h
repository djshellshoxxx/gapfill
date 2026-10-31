#pragma once

#include "Sources.h"
#include "../macros/MacroEngine.h"

#include <cstddef>

namespace gapfill::modulation
{

// The shipped 0..35 indices and their meanings never change. The detailed
// specification's conflicting indices are implemented only as appended targets.
inline constexpr int kTargetCount = 47;
inline constexpr std::size_t kMaxStateBytes = 8 * 1024;
inline constexpr const char* kSubtreeName = "MacrosV3";

struct ExtendedConfig
{
    char names[kMacroCount][macros::kNameBytes + 1] {};
    std::uint8_t count[kMacroCount] {};
    macros::Mapping map[kMacroCount][macros::kMaxMappings] {};
    macros::MacroConfig retainedLegacy; ///< Frozen opt-in snapshot, including all four legacy LFOs.
};

ExtendedConfig extendedFromLegacy (const macros::MacroConfig&);
static_assert (std::is_trivially_copyable_v<ExtendedConfig>);

} // namespace gapfill::modulation
