#pragma once

// F-L09 Groove inheritance: the warp from a learned groove to sub-event shifts.
//
// A(ppq) is a periodic piecewise-linear shift (in beats) through the slot
// points A(k*g) = offset[k mod 8] * g * amount, clamped to +-0.35 step. Its
// maximum slope is 0.7 < 1, so the warp is strictly monotonic: subdivisions
// are moved, never reordered.

#include "GrooveTracker.h"

namespace gapfill::groove
{

inline constexpr int kMaxShifts = 64;
inline constexpr float kMaxShiftSteps = 0.35f;

/** Per-fill shifts of the repeat boundaries, in samples. Neutral = on false. */
struct GrooveShifts
{
    bool on = false;
    int  count = 0;
    int  shift[kMaxShifts] {};

    /** Shift of boundary k relative to boundary 0 (the fill start never moves). */
    int at (int k) const noexcept { return (on && k >= 0 && k < count) ? shift[k] : 0; }

    /** Change of the length of repeat k: boundary k+1 minus boundary k. */
    int delta (int k) const noexcept { return on ? at (k + 1) - at (k) : 0; }
};

/** Shift in beats at host position `ppq`; 0 for an invalid snapshot or amount 0. */
double warpBeats (const GrooveSnapshot&, int grid, double ppq, float amount01) noexcept;

/** Boundaries t_k = fillStartPpq + k * periodBeats, k < kMaxShifts. All shifts are
    relative to boundary 0, clamped to +-0.35 period and kept increasing by >= 0.3
    period. `out.on` is false (and nothing is written) when the groove is invalid
    or amount is 0. */
void makeShifts (const GrooveSnapshot&, int grid, double fillStartPpq, double periodBeats,
                 double samplesPerBeat, float amount01, GrooveShifts& out) noexcept;

} // namespace gapfill::groove
