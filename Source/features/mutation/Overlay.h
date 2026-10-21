#pragma once

// F-L17 preview overlay (audio side). The message thread pushes OverlayMsg into a small SPSC queue; the
// audio thread drains it inside apply(), which runs as the FIRST stage of the effective-settings pipeline
// (before personality and macros, so those still act on top). Inactive = apply() returns before touching
// EngineSettings (the memcmp test). No allocation, no locks, no RNG.

#include "Genome.h"

#include "../../dsp/EngineSettings.h"
#include "../../dsp/SpscQueue.h"

namespace gapfill::mutation
{

struct OverlayMsg
{
    bool active = false;
    float v[kNumGenes] {};
};

class OverlayState
{
public:
    /** Message thread (single producer). False when the queue is full: the caller retries on its next tick. */
    bool push (const OverlayMsg& m) noexcept   { return queue.push (m); }

    /** Audio thread. Const so the (const) settings gatherer can call it; the state is mutable. */
    void apply (EngineSettings& s) const noexcept;

    bool isActiveForTests() const noexcept     { return active; }

private:
    mutable SpscQueue<OverlayMsg, 4> queue;
    mutable OverlayMsg current;
    mutable bool active = false;
};

} // namespace gapfill::mutation
