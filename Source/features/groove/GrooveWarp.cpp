#include "GrooveWarp.h"

#include <algorithm>
#include <cmath>

namespace gapfill::groove
{

double warpBeats (const GrooveSnapshot& snap, int grid, double ppq, float amount01) noexcept
{
    if (! snap.valid || amount01 <= 0.0f || ! std::isfinite (ppq))
        return 0.0;

    const double g = grid == 0 ? 0.5 : 0.25;
    const double u = ppq / g;
    const double k0 = std::floor (u);
    const double frac = u - k0;

    const auto point = [&] (double k)
    {
        const int idx = (int) (((long long) k % kSlots + kSlots) % kSlots);
        const double a = (double) snap.offsetSteps[idx] * g * (double) std::clamp (amount01, 0.0f, 1.0f);
        const double lim = (double) kMaxShiftSteps * g;
        return std::clamp (a, -lim, lim);
    };

    return point (k0) * (1.0 - frac) + point (k0 + 1.0) * frac;
}

void makeShifts (const GrooveSnapshot& snap, int grid, double fillStartPpq, double periodBeats,
                 double samplesPerBeat, float amount01, GrooveShifts& out) noexcept
{
    out = {};

    if (! snap.valid || amount01 <= 0.0f || periodBeats <= 0.0 || samplesPerBeat <= 1.0)
        return;

    const double periodSamples = periodBeats * samplesPerBeat;
    const double lim = (double) kMaxShiftSteps * periodSamples;
    const double base = warpBeats (snap, grid, fillStartPpq, amount01) * samplesPerBeat;

    out.on = true;
    out.count = kMaxShifts;
    double prev = 0.0;

    for (int k = 0; k < kMaxShifts; ++k)
    {
        const double a = warpBeats (snap, grid, fillStartPpq + (double) k * periodBeats, amount01) * samplesPerBeat - base;
        double s = std::clamp (a, -lim, lim);

        if (k == 0)
            s = 0.0;
        else
            s = std::max (s, prev - 0.7 * periodSamples);        // t_k + s_k increases by >= 0.3 period

        out.shift[k] = (int) std::lround (s);
        prev = s;
    }
}

} // namespace gapfill::groove
