#pragma once

// Bar arithmetic shared by the decision-stage tickets (C-6 Breathing, C-7 Phrase,
// C-8 Evolve, F-D01 Budget). Pure and block-size independent: everything is a
// function of the PPQ of the decision sample and the host time signature.

#include "../effective/Effective.h"

#include <cmath>

namespace gapfill::timing
{

inline bool isPowerOfTwo (int v) noexcept   { return v > 0 && (v & (v - 1)) == 0; }

/** Quarter-note beats per bar for tsNum/tsDen, or 0 when the signature is not usable
    (needs tsNum > 0 and tsDen a positive power of two). */
inline double beatsPerBar (int tsNum, int tsDen) noexcept
{
    if (tsNum <= 0 || tsNum > 64 || ! isPowerOfTwo (tsDen) || tsDen > 64)
        return 0.0;

    return (double) tsNum * 4.0 / (double) tsDen;
}

/** Beats per bar if the transport info is usable for bar maths (valid transport,
    finite PPQ, known time signature); 0 otherwise. */
inline double beatsPerBar (const Effective::TransportInfo& t) noexcept
{
    if (! t.valid || ! std::isfinite (t.ppq))
        return 0.0;

    return beatsPerBar (t.tsNum, t.tsDen);
}

/** Fractional part in [0, 1) that is also correct for negative values. */
inline double frac (double x) noexcept
{
    const auto f = x - std::floor (x);
    return f >= 1.0 ? 0.0 : f;
}

/** Index of the bar containing `ppq` (anchored at PPQ 0, so loops and seeks reproduce). */
inline long long barIndex (double ppq, double beatsPerBarValue) noexcept
{
    return (long long) std::floor (ppq / beatsPerBarValue);
}

} // namespace gapfill::timing
