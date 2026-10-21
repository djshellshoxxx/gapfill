#include "Budget.h"
#include "../phrase/Phrase.h"
#include "../timing/BarTiming.h"

#include <cmath>

namespace gapfill::budget
{

void RecentFills::push (std::int64_t tick) noexcept
{
    ticks[(size_t) head] = tick;
    head = (head + 1) % capacity;
    count = count < capacity ? count + 1 : capacity;
}

int RecentFills::countInBar (long long barIdx, double beatsPerBar) const noexcept
{
    if (beatsPerBar <= 0.0)
        return 0;

    int n = 0;

    for (int i = 0; i < count; ++i)
    {
        const auto ppq = (double) ticks[(size_t) i] / (double) kTicksPerBeat;

        if (timing::barIndex (ppq, beatsPerBar) == barIdx)
            ++n;
    }

    return n;
}

double positionInBar (double ppq, double beatsPerBar) noexcept
{
    const auto bar = std::floor (ppq / beatsPerBar);
    const auto pos = ppq - bar * beatsPerBar;
    return pos < 0.0 ? 0.0 : (pos >= beatsPerBar ? 0.0 : pos);
}

bool inQuietZone (int zone, double ppq, double beatsPerBar, const PhraseSettings& ph,
                  const Effective::TransportInfo& t) noexcept
{
    switch (zone)
    {
        case Beat1:        return positionInBar (ppq, beatsPerBar) < 1.0;
        case LastBeat:     return positionInBar (ppq, beatsPerBar) >= beatsPerBar - 1.0;
        case LastTwoBeats: return positionInBar (ppq, beatsPerBar) >= beatsPerBar - 2.0;
        case LastBarOfPhrase: return phrase::inFinalBar (ph, ppq, t);
        default:           return false;
    }
}

RejectReason evaluate (const BudgetSettings& b, const PhraseSettings& ph, double ppq,
                       const Effective::TransportInfo& t, const RecentFills& recent,
                       bool* needsTransport) noexcept
{
    if (needsTransport != nullptr)
        *needsTransport = false;

    if (b.perBar <= 0 && b.zone <= ZoneOff)
        return RejectReason::None;

    const auto bpb = timing::beatsPerBar (t);

    if (bpb <= 0.0 || ! std::isfinite (ppq))
    {
        if (needsTransport != nullptr)
            *needsTransport = true;

        return RejectReason::None;                         // neutral without transport
    }

    if (b.perBar > 0 && recent.countInBar (timing::barIndex (ppq, bpb), bpb) >= b.perBar)
        return RejectReason::BudgetFull;

    if (b.zone > ZoneOff && inQuietZone (b.zone, ppq, bpb, ph, t))
        return RejectReason::QuietZone;

    return RejectReason::None;
}

} // namespace gapfill::budget
