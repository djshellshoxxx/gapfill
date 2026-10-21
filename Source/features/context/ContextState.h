#pragma once

// Musical-context state owned by the processor (WP7): one object, three calls.
//   prepare / reset     lifecycle (prepareToPlay, releaseResources, transport reset)
//   onChunk             audio thread, once per processed chunk, after the engine
//                       settings were gathered: feeds the trackers and publishes
//                       their results into EngineSettings (read by the effective
//                       stages and the voice hooks)
// Features of the group (F-L22 energy; later F-X04 sections, F-X03 key detection)
// add their tracker here and one line in onChunk.

#include "EnergyTracker.h"
#include "../groove/GrooveState.h"
#include "../keydetect/AutoKey.h"
#include "../../Parameters.h"
#include "../../dsp/EngineSettings.h"

#include <atomic>

namespace gapfill::context
{

/** Reads the energy follow parameters into s.perf.energy (neutral = adapt 0). */
void gatherContext (EngineSettings& s, const ParamCache& p) noexcept;

class ContextState
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Audio thread. `mono` is the live main input of this chunk. */
    void onChunk (const float* mono, int numSamples, const AnalysisEngine&, std::int64_t chunkInputStart,
                  EngineSettings& s) noexcept;

    /** Audio thread: the commands the context features own (GrooveLearn / GrooveReset). */
    void onCommand (const Command& c) noexcept   { groove.onCommand (c); }

    /** Audio thread: the host transport jumped. */
    void onTransportJump() noexcept              { groove.onTransportJump(); }

    /** Message thread: 0 Low, 1 Mid, 2 High, -1 while off or still learning. */
    int energyLevelForDisplay() const noexcept { return displayLevel.load (std::memory_order_relaxed); }

    const EnergyTracker& energy() const noexcept { return energyTracker; }
    groove::GrooveState& grooveState() noexcept { return groove; }
    keydetect::AutoKey& autoKey() noexcept { return key; }
    const keydetect::AutoKey& autoKey() const noexcept { return key; }
    const groove::GrooveState& grooveState() const noexcept { return groove; }

private:
    EnergyTracker energyTracker;
    groove::GrooveState groove;
    keydetect::AutoKey key;
    bool energyRunning = false;
    std::atomic<int> displayLevel { -1 };
};

} // namespace gapfill::context
