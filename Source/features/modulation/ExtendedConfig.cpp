#include "ExtendedConfig.h"

#include <cstring>

namespace gapfill::modulation
{
ExtendedConfig extendedFromLegacy (const macros::MacroConfig& legacy)
{
    ExtendedConfig result;
    std::memcpy (&result.retainedLegacy, &legacy, sizeof (legacy));
    for (int macro = 0; macro < macros::kNumMacros; ++macro)
    {
        std::memcpy (result.names[macro], legacy.names[macro], sizeof (legacy.names[macro]));
        result.count[macro] = legacy.count[macro];
        std::memcpy (result.map[macro], legacy.map[macro], sizeof (legacy.map[macro]));
    }
    constexpr const char* extraNames[] { "Macro 5", "Macro 6", "Macro 7", "Macro 8" };
    for (int macro = 4; macro < kMacroCount; ++macro)
        std::strncpy (result.names[macro], extraNames[macro - 4], macros::kNameBytes);
    return result;
}
} // namespace gapfill::modulation
