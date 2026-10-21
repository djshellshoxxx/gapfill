#pragma once

#include "EngineEvents.h"
#include "EngineSettings.h"
#include "FillVoice.h"
#include "SchedulerTypes.h"
#include "VisualisationState.h"

#include <array>
#include <limits>

namespace gapfill
{

//==============================================================================
/** Schedules fills into detected gaps and renders the voices that result.

    The scheduler runs on the analysis hop grid so its decisions land on exactly
    the same sample positions no matter how the host slices its blocks. */
class FillEngine
{
    friend struct SchedulerTestAccess;   // unit tests drive single stages directly

public:
    static constexpr int numVoices = 4;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Fades every voice out quickly (bypass, transport jumps, reset). */
    void panic() noexcept;

    /** Forgets the deterministic gap counter, used when the transport jumps. */
    void noteTransportJump() noexcept { gapIndex = 0; }

    /** Renders into `wet` (which must already be cleared) and returns the
        protection duck that the processor should apply to the wet bus. */
    void process (juce::AudioBuffer<float>& wet, int wetStartSample, int numSamples,
                  std::int64_t outputStartPos,
                  const AnalysisEngine& analysis,
                  const SourceHistory& history,
                  const EngineSettings& settings,
                  VisualisationFifo* viz,
                  FillEventQueue* events = nullptr) noexcept;

    int  getActiveVoiceCount() const noexcept;
    int  getCurrentAlgo() const noexcept { return lastRenderedAlgo; }
    float getCurrentDuck() const noexcept { return currentDuck; }

    /** Total number of fills started since the last reset (used by tests). */
    int  getFillsStarted() const noexcept { return fillsStarted; }

    /** Number of eligible gaps rejected because they were shorter than the
        Minimum Gap setting (used by tests). */
    int  getGapsRejectedTooShort() const noexcept { return gapsTooShort; }

    /** Gaps rejected for each RejectReason since the last reset (index = reason;
        audio-thread owned, read by tests and published to the meters). */
    int  getRejectCount (RejectReason r) const noexcept { return rejectCounts[(size_t) r]; }

private:
    void controlUpdate (std::int64_t outputPos, std::int64_t blockStartPos,
                        const AnalysisEngine& analysis, const SourceHistory& history,
                        const EngineSettings& s, VisualisationFifo* viz,
                        FillEventQueue* events) noexcept;

    // --- scheduler stages (docs/dev/02-FOUNDATIONS.md F0-2) -------------------
    // trySchedule is a short orchestrator; each stage below is owned by the
    // feature tickets named in the spec. The RNG draw order is FROZEN:
    // chooseBand (spectral) -> chance(probability) -> nextBipolar (length scale)
    // -> chooseAlgorithm. New features never draw from `rng`.

    ScheduleResult trySchedule (std::int64_t outputPos, std::int64_t blockStartPos,
                                const AnalysisFrame& frame, const AnalysisEngine& analysis,
                                const SourceHistory& history, const EngineSettings& s) noexcept;

    /** Fill on, source present, voices free, not too soon, enough room.
        Fills `ctx` (room figures) on success; counts TooShort gaps. */
    RejectReason checkEligibility (GapContext& ctx, const AnalysisEngine& analysis,
                                   const EngineSettings& s, float& neededMs) noexcept;

    /** Spectral lane choice and the probability roll. Draws from `rng`. */
    RejectReason rollDecision (GapContext& ctx, Rng& rng, const EngineSettings& s,
                               int& band) noexcept;

    /** Length and algorithm. Draws from `rng`. */
    RejectReason buildRecipe (const GapContext& ctx, Rng& rng, const EngineSettings& s,
                              int band, FillPlan& plan) noexcept;

    /** HOOK for Lock (C-5) and Mistakes (C-3): may rewrite the plan, or return
        a reject reason (LockSkipped). Empty in Phase 0. */
    RejectReason adjustRecipe (FillPlan& plan, const GapContext& ctx, const EngineSettings& s) noexcept;

    VoiceSetup toVoiceSetup (const FillPlan& plan, const GapContext& ctx, const EngineSettings& s,
                             const SourceHistory& history) const noexcept;

    /** Starts the voice, with the Granular/Reverse fallback when the spectral
        processor is busy. Updates `plan` if the algorithm changed. */
    bool startVoice (FillVoice& voice, VoiceSetup& setup, FillPlan& plan,
                     const SourceHistory& history) noexcept;

    void noteFillStarted (const FillPlan& plan, std::int64_t outputPos) noexcept;
    void noteRejected (RejectReason r) noexcept;

    int chooseAlgorithm (float lengthMs, const EngineSettings& s, Rng& rng) noexcept;
    int chooseBand (const AnalysisFrame& frame, const EngineSettings& s, Rng& rng) noexcept;
    FillVoice* findFreeVoice() noexcept;

    double sr = 48000.0;
    int channels = 2;

    FillVoice voices[numVoices];
    SpectralSmear spectral;

    // --- gap state machine ---
    bool inGap = false;
    bool gapEvaluated = false;
    std::int64_t gapStartPos = 0;
    std::int64_t lastFillStartPos = -1000000;
    std::int64_t lastEventPos = 0;
    float lastEventTransient = 0.5f;
    std::int64_t gapIndex = 0;

    // --- protection envelopes ---
    // Protect Hold defines a safe region immediately after a detected hit in
    // which the wet path stays fully protected; after it the protection
    // releases smoothly rather than jumping back.
    float transEnv = 0.0f, scEnv = 0.0f;
    std::int64_t lastMainEventPos = std::numeric_limits<std::int64_t>::min() / 4;
    std::int64_t lastScEventPos   = std::numeric_limits<std::int64_t>::min() / 4;
    float currentDuck = 1.0f;

    // --- recent algorithm history ---
    static constexpr int historyLength = 4;
    int recentAlgos[historyLength];
    int recentWrite = 0;

    int lastRenderedAlgo = -1;
    int fillsStarted = 0;
    int gapsTooShort = 0;
    std::array<int, (size_t) numRejectReasons> rejectCounts {};

    // --- visualiser decimation ---
    int vizDecim = 2;
    int vizCounter = 0;
    VizFrame vizAccum;
};

} // namespace gapfill
