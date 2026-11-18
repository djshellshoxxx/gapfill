// U-9: help text for the reserved F-M18 detailed Beat Battle controls (BATTLEV2, host indices 295-297).
// The detailed runtime is not wired yet (docs/progress/battle-v2-contract-20261031.md), so each line says so.

#include "../info/ParamInfo.h"

namespace gapfill::info
{
namespace
{
    const ParamInfoRegistrar battleV2Info
    {
        { "fm18_on",    "Detailed Battle",    "Reserved for the detailed Beat Battle; not active in this build", "", "" },
        { "fm18_unit",  "Battle window",      "Reserved: bars per detailed Battle window (1, 2, 4 or 8); not active yet", "", "" },
        { "fm18_first", "First Battle style", "Reserved: which style plays in the first detailed window; not active yet", "", "" },
    };
}
} // namespace gapfill::info
