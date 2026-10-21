#include "SchedulerTypes.h"

namespace gapfill
{

const char* rejectReasonName (RejectReason r) noexcept
{
    switch (r)
    {
        case RejectReason::None:              return "None";
        case RejectReason::FillOff:           return "FillOff";
        case RejectReason::NoSource:          return "NoSource";
        case RejectReason::TooSoon:           return "TooSoon";
        case RejectReason::NoVoice:           return "NoVoice";
        case RejectReason::TooShort:          return "TooShort";
        case RejectReason::GuardEaten:        return "GuardEaten";
        case RejectReason::NoBand:            return "NoBand";
        case RejectReason::DensityRoll:       return "DensityRoll";
        case RejectReason::NoAlgoFits:        return "NoAlgoFits";
        case RejectReason::SourceUnavailable: return "SourceUnavailable";
        case RejectReason::LockSkipped:       return "LockSkipped";
        case RejectReason::BudgetFull:        return "BudgetFull";
        case RejectReason::QuietZone:         return "QuietZone";
        case RejectReason::ScGated:           return "ScGated";
        case RejectReason::Panicked:          return "Panicked";
        case RejectReason::CoopYield:         return "CoopYield";
        case RejectReason::ForcedUnsafe:      return "ForcedUnsafe";
        case RejectReason::Count:             break;
    }

    return "-";
}

} // namespace gapfill
