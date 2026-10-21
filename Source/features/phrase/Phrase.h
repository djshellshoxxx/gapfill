#pragma once

// C-7 Musical Phrase Awareness (docs/dev/04-SPEC-CREATIVE.md C-7, doc 12 F-L08 offset).
//
// Decision stage. Inside the final bar of a phrase the fill PROBABILITY is
// multiplied by 1 + bias * 1.5 * r, with r = smoothstep (f, 1, p),
// f = 1 - 1/bars and p the position in the phrase. The multiplier is written
// to EngineSettings::perf.probabilityMul (the scheduler then clamps the final
// probability to 1); it never creates a gap or overrides eligibility.
// Outside the last bar the multiplier is exactly 1 and nothing is written.

#include "../effective/Effective.h"

namespace gapfill::phrase
{

inline constexpr int kBarChoices[5] { 0, 2, 4, 8, 16 };   // phraseBars choice index -> bars (0 = Off)

/** Position in the phrase p in [0, 1) at `ppq`, or a negative number when the phrase
    cannot be evaluated (off, invalid transport, unknown time signature). */
double positionAt (const PhraseSettings&, double ppq, const Effective::TransportInfo&) noexcept;

/** True while `ppq` lies in the final bar of the phrase (position >= 1 - 1/bars). Independent of
    the bias, so F-D01 "Last bar of phrase" works with Bias 0. False when the phrase is Off, the
    transport is invalid or the time signature unknown. */
bool inFinalBar (const PhraseSettings&, double ppq, const Effective::TransportInfo&) noexcept;

/** r = smoothstep (1 - 1/bars, 1, p) in [0, 1]. */
float rampAt (double position, int bars) noexcept;

/** Probability multiplier for ramp value r and bias in [0, 1]: 1 + bias * 1.5 * r. */
float multiplierFor (float bias01, float r) noexcept;

void apply (EngineSettings&, double ppq, const Effective::TransportInfo&) noexcept;

} // namespace gapfill::phrase
