#pragma once

// Per-block side channel between FillEngine::process and the bus effect chain
// (docs/dev/12 FX-0 / F-L14). All pointers are borrowed for one block; the
// engine only writes what the chain asked for, so a null `io` (or null members)
// costs nothing and leaves the engine's output untouched.

#include "EffectCard.h"

namespace gapfill::fx
{

struct EngineBlockIO
{
    /** F-L14: when non-null (and cleared by the caller) voices whose band
        has effects disabled render here instead of into the main wet bus, so
        BusFx never sees them. Same channel count / length as the wet buffer. */
    juce::AudioBuffer<float>* plainWet = nullptr;

    /** Per-sample protection duck, indexed like the wet buffer. Published only
        while a BusFx is enabled. */
    float* duck = nullptr;

    /** Fills started during the block (written by the engine, <= kMaxFillStarts). */
    FillStartInfo starts[kMaxFillStarts] {};
    int numStarts = 0;
};

} // namespace gapfill::fx
