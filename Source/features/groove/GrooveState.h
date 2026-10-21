#pragma once

// F-L09 Groove inheritance: the processor-owned runtime state.
//
// Audio thread: onChunk (feeds the tracker from the analysis events, publishes
// the snapshot), onCommand (GrooveLearn / GrooveReset), onTransportJump.
// Other threads: readSnapshot (seqlock, never blocks the audio thread),
// requestLoad (state restore), learnBarsRemaining.

#include "GrooveSettings.h"
#include "../../dsp/AnalysisEngine.h"
#include "../../dsp/EngineEvents.h"
#include "../../dsp/EngineSettings.h"

#include <atomic>

namespace gapfill::groove
{

class GrooveState
{
public:
    static constexpr int kLearnBars = 8;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;                              ///< lifecycle reset (keeps a held groove)

    /** Audio thread. `chunkInputStart` is the input sample position of the chunk's
        first sample; the engine settings already carry the transport. */
    void onChunk (const AnalysisEngine&, std::int64_t chunkInputStart, int numSamples, EngineSettings&) noexcept;
    void onCommand (const Command&) noexcept;
    void onTransportJump() noexcept;

    // ---- any thread ------------------------------------------------------------
    bool readSnapshot (GrooveSnapshot& out) const noexcept;        ///< false while a write is in progress
    int  learnBarsRemaining() const noexcept { return learnRemaining.load (std::memory_order_relaxed); }
    bool isFrozenByLearn() const noexcept   { return latched.load (std::memory_order_relaxed); }
    bool isWaitingForTempo() const noexcept { return waiting.load (std::memory_order_relaxed); }

    /** Message thread: restores a held groove at the next chunk. */
    void requestLoad (const GrooveSnapshot&, bool latchedByLearn) noexcept;
    void requestReset() noexcept { resetRequested.store (true, std::memory_order_release); }

    const GrooveTracker& tracker() const noexcept { return trk; }

private:
    void publish (const GrooveSnapshot&) noexcept;

    GrooveTracker trk;
    double sr = 48000.0;
    bool havePrevPpq = false;
    double expectedPpq = 0.0;
    bool learning = false;
    double learnBeatsLeft = 0.0;
    std::int64_t lastScanned = 0;
    bool scannedValid = false;
    GrooveSnapshot heldSnap;
    bool heldValid = false;

    std::atomic<bool> latched { false };
    std::atomic<bool> waiting { false };
    std::atomic<int> learnRemaining { 0 };
    std::atomic<bool> resetRequested { false };

    // seqlock for the published snapshot
    std::atomic<std::uint32_t> seq { 0 };
    std::atomic<float> pubOffsets[kSlots];
    std::atomic<float> pubConfidence { 0.0f }, pubSwing { 50.0f };
    std::atomic<bool> pubValid { false };

    // pending load from the message thread
    std::atomic<std::uint32_t> loadSeq { 0 };
    std::atomic<float> loadOffsets[kSlots];
    std::atomic<bool> loadPending { false }, loadLatched { false };
};

} // namespace gapfill::groove
