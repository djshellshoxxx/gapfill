#pragma once

// F-M17 Wet-only output mode (docs/dev/12 F-M17).
//
// `wetSolo` is a routing switch: while on, the main output is the wet bus at
// unity (after protection, width, mono-wet and wetSafety) and the Dry / Wet
// knobs are ignored. The Output trim and host bypass still apply, and Panic
// still silences the wet bus (it is a wet-bus gain). The processor feeds the
// result into its existing 20 ms dry / wet smoothers, so switching is
// click-free and the arithmetic of the mix loop is untouched.
//
// Pure function, no state: tested without a processor.

namespace gapfill::wetout
{

struct MixTargets
{
    float dry, wet;
};

/** Targets for the dry / wet gain smoothers. `dry01` / `wet01` are the knob values (0..1).
    Off returns the knobs unchanged (bit-identical to the pre-feature mix). */
inline MixTargets mixTargets (bool wetSolo, float dry01, float wet01) noexcept
{
    return wetSolo ? MixTargets { 0.0f, 1.0f } : MixTargets { dry01, wet01 };
}

} // namespace gapfill::wetout
