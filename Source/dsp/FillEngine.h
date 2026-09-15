#pragma once

#include "FillVoice.h"
#include "VisualisationState.h"

namespace gapfill
{

//==============================================================================
/** Everything the scheduler needs, captured once per block from the parameters
    and the host playhead. */
struct EngineSettings
{
    DetMode mode = DetMode::Transient;

    float fill01       = 0.35f;
    float intensity01  = 0.50f;
    float complexity01 = 0.40f;
    float chaos01      = 0.25f;
    float density01    = 0.65f;

    /** 0 = Auto, otherwise (Algo index + 1). */
    int   fillTypeIndex = 0;
    float weights[numAlgos] {};

    int   seed = 1;

    float minGapMs      = 60.0f;
    float maxFillMs     = 400.0f;
    float protectHoldMs = 40.0f;
    float lookaheadMs   = 40.0f;

    float protTrans01 = 0.60f;
    float protNext01  = 0.70f;
    float protSC01    = 0.70f;

    float stutterSeconds = 0.125f;
    float delaySeconds   = 0.125f;
    bool  pingPong       = true;

    float xoverLM = 180.0f;
    float xoverMH = 3500.0f;

    bool  bandOn[3]   { true, true, true };
    float bandAmt[3]  { 1.0f, 1.0f, 1.0f };
    float bandInt[3]  { 0.5f, 0.5f, 0.5f };
    float bandProb[3] { 0.45f, 0.75f, 0.9f };

    double ppqAtBlockStart = 0.0;
    double samplesPerBeat  = 0.0;
    bool   transportValid  = false;
    bool   sidechainActive = false;
};

//==============================================================================
/** Schedules fills into detected gaps and renders the voices that result.

    The scheduler runs on the analysis hop grid so its decisions land on exactly
    the same sample positions no matter how the host slices its blocks. */
class FillEngine
{
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
                  VisualisationFifo* viz) noexcept;

    int  getActiveVoiceCount() const noexcept;
    int  getCurrentAlgo() const noexcept { return lastRenderedAlgo; }
    float getCurrentDuck() const noexcept { return currentDuck; }

    /** Total number of fills started since the last reset (used by tests). */
    int  getFillsStarted() const noexcept { return fillsStarted; }

    /** Number of eligible gaps rejected because they were shorter than the
        Minimum Gap setting (used by tests). */
    int  getGapsRejectedTooShort() const noexcept { return gapsTooShort; }

private:
    void controlUpdate (std::int64_t outputPos, std::int64_t blockStartPos,
                        const AnalysisEngine& analysis, const SourceHistory& history,
                        const EngineSettings& s, VisualisationFifo* viz) noexcept;

    bool trySchedule (std::int64_t outputPos, std::int64_t blockStartPos,
                      const AnalysisFrame& frame, const AnalysisEngine& analysis,
                      const SourceHistory& history, const EngineSettings& s) noexcept;

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

    // --- protection envelopes (hop rate) ---
    float transEnv = 0.0f, scEnv = 0.0f;
    float transDecay = 0.9f, scDecay = 0.9f;
    float currentDuck = 1.0f;

    // --- recent algorithm history ---
    static constexpr int historyLength = 4;
    int recentAlgos[historyLength];
    int recentWrite = 0;

    int lastRenderedAlgo = -1;
    int fillsStarted = 0;
    int gapsTooShort = 0;

    // --- visualiser decimation ---
    int vizDecim = 2;
    int vizCounter = 0;
    VizFrame vizAccum;
};

} // namespace gapfill
