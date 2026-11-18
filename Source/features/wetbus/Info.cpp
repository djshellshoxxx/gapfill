#include "../info/ParamInfo.h"

namespace gapfill::info
{
namespace
{
    const ParamInfoRegistrar wetBusInfo
    {
        { "wetBusMainMode", "Main output mode", "Dry only removes fills from Main while the separate Wet output is enabled", "", "" },
        { "wetBusLevel", "Wet bus level", "Trim the separate Wet output without changing the main mix or export tap", "-24 to +12", "dB" },
    };
}
} // namespace gapfill::info
