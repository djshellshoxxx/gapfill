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

    /** WP9: tempo / scale / source for the bus cards, written by the engine hook
        at the top of FillEngine::process. 0 = no valid transport. */
    double samplesPerBeat = 0.0;
    std::uint16_t scaleMask = 0;      ///< see BusFxContext (N-MUS seam)
    int scaleRoot = 0;
    const SourceHistory* history = nullptr;
    std::int64_t historyOffset = 0;

    /** Fills started during the block (written by the engine, <= kMaxFillStarts). */
    FillStartInfo starts[kMaxFillStarts] {};
    int numStarts = 0;
};

} // namespace gapfill::fx
