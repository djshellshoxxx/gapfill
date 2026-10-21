#pragma once

// F-M22 smart preset suggestions: audio-thread input profile (docs/dev/features/F-M22-smart-preset-suggestions.md).
//
// While the user has pressed Listen, the processor calls accumulate() once per chunk, directly after
// analysis.processBlock and BEFORE the engine discards events. It reads only the analysis frames and the
// detection-event queue: no audio is stored, only a 128-byte accumulator. Idle = one relaxed atomic load.
// Accumulation uses absolute hop indices, so the result does not depend on the host block size.
//
// Threading: startListening / cancel / takeProfile / prepare = message thread; accumulate = audio thread.

#include "../../dsp/AnalysisEngine.h"

#include <atomic>
#include <cstdint>

namespace gapfill::suggest
{

inline constexpr float kMinSeconds = 4.0f, kMaxSeconds = 30.0f, kDefaultSeconds = 10.0f;

struct InputProfile
{
    float eventsPerSec = 0.0f;
    float gapFraction = 0.0f;
    float levelDb = -60.0f;
    float lowShare = 0.0f;
    float highShare = 0.0f;
    float seconds = 0.0f;
    bool valid = false;
};

class ProfileAccumulator
{
public:
    enum class State : int { Idle = 0, Requested, Listening, Done, NoSignal };

    /** Message thread (prepareToPlay / editor). Also abandons a run: the rate it started with is no longer valid. */
    void prepare (double sampleRate) noexcept;

    /** Message thread. The audio thread picks the request up at the next chunk. */
    void startListening (float seconds) noexcept;
    void cancel() noexcept;

    /** Audio thread. */
    void accumulate (const AnalysisEngine& analysis, std::int64_t fromPos, std::int64_t toPos) noexcept
    {
        if (state.load (std::memory_order_relaxed) < (int) State::Requested
             || state.load (std::memory_order_relaxed) > (int) State::Listening)
            return;

        run (analysis, fromPos, toPos);
    }

    State getState() const noexcept   { return (State) state.load (std::memory_order_acquire); }
    float getProgress() const noexcept;

    /** Message thread. True once per finished run (state Done); NoSignal returns false with valid=false. */
    bool takeProfile (InputProfile&) noexcept;

private:
    void run (const AnalysisEngine&, std::int64_t fromPos, std::int64_t toPos) noexcept;
    void finish() noexcept;

    std::atomic<int> state { (int) State::Idle };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<std::int64_t> requestedHopsMillis { 0 };     // requested duration in ms
    std::atomic<std::int64_t> targetHops { 0 }, doneHops { 0 };

    // Audio-thread-only accumulators (reset when a request is picked up).
    std::int64_t nextHop = 0, hops = 0, gapHops = 0, signalHops = 0, events = 0, eventCursor = 0;
    double sumRef = 0.0, sumLow = 0.0, sumMid = 0.0, sumHigh = 0.0;

    // Single-writer seqlock publishing the profile once per run.
    std::atomic<std::uint32_t> seq { 0 };
    InputProfile published;
    std::atomic<bool> fresh { false };
};

} // namespace gapfill::suggest
