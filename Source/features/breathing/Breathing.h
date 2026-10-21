#pragma once

// C-6 Density Breathing (docs/dev/04-SPEC-CREATIVE.md C-6): a slow, bar-locked
// swell of the Density control, evaluated per gap on the scheduler's local copy.
//
// density' = clamp (density + 0.5 * depth * c(phase), 0, 1) with
// phase = frac (ppq / (bars * beatsPerBar)), anchored at PPQ 0.
// Neutral (nothing touched) when off, depth 0, transport invalid or the time
// signature unknown.

#include "../effective/Effective.h"

namespace gapfill::breathing
{

enum Curve { Sine = 0, RiseFall, Pulse, NumCurves };

inline constexpr int kBarChoices[4] { 1, 2, 4, 8 };   // densityBreathBars choice index -> bars

/** c(phase) in [-1, 1]. Sine sin(2 pi phase); RiseFall triangle; Pulse +1 / -1. */
float curveValue (int curve, double phase) noexcept;

/** Phase in [0, 1) at `ppq`, or a negative number when breathing cannot be evaluated. */
double phaseAt (const BreathSettings&, double ppq, const Effective::TransportInfo&) noexcept;

/** The decision-stage function (called by Effective::applyBreathing). */
void apply (EngineSettings&, double ppq, const Effective::TransportInfo&) noexcept;

} // namespace gapfill::breathing
