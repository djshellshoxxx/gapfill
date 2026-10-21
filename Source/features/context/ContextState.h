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
#include "../taste/TasteService.h"
#include "../tension/Tension.h"
#include "../arrangement/SectionTracker.h"
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
    void onCommand (const Command& c) noexcept   { groove.onCommand (c); taste.onCommand (c); }

    /** Message thread (processor timer / fill-event drain): taste ratings and the library file. */
    void timerTick (std::uint32_t nowMs, bool tasteOn, float tasteStrength01) { taste.timerTick (nowMs, tasteOn, tasteStrength01); }
    void onFillEvent (const FillEvent& e) noexcept { taste.onFillEvent (e); }
    tension::TensionService& tensionService() noexcept { return tensionSvc; }
    const tension::TensionService& tensionService() const noexcept { return tensionSvc; }

    /** Audio thread: width target delta for this chunk (F-M21); 0 while Tension is off. */
    float tensionWidthDelta (const EngineSettings& s, float baseWidth01, int numSamples) const noexcept;

    /** Any thread: the section tracker's last published state (a torn read costs one wrong label). */
    arrangement::ArrangementState arrangementState() const noexcept;
    int arrangementFrames() const noexcept { return arrFrames.load (std::memory_order_relaxed); }

    taste::TasteService& tasteService() noexcept { return taste; }
    const taste::TasteService& tasteService() const noexcept { return taste; }

    /** Audio thread: the host transport jumped. */
    void onTransportJump() noexcept              { groove.onTransportJump(); section.reset(); publishArrangement(); }

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
    taste::TasteService taste;
    tension::TensionService tensionSvc;
    arrangement::SectionTracker section;
    bool arrRunning = false;

    void publishArrangement() noexcept;
    std::atomic<std::uint64_t> arrWord0 { 0 }, arrWord1 { 0 };
    std::atomic<int> arrFrames { 0 };
    bool energyRunning = false;
    std::atomic<int> displayLevel { -1 };
};

} // namespace gapfill::context
