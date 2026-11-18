#include "../info/ParamInfo.h"

namespace gapfill::info
{
namespace
{
    const ParamInfoRegistrar captureExtensionInfo
    {
        { "fm08_detail", "Detailed recapture", "Record live input only while held and report short, busy or cancelled recaptures", "", "" },
        { "fm08_scan", "Capture Scan", "Vary each fill's start within held material; 0 always uses the classic source anchor", "0-100", "%" },
    };
}
} // namespace gapfill::info
