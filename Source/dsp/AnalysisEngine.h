#pragma once

#include "DspUtils.h"
#include "../Parameters.h"

namespace gapfill
{

//==============================================================================
/** One hop's worth of analysis results.

    Frames live in a ring indexed by absolute sample position / hopSize, so the
    scheduler - which runs `lookahead` samples behind the analysis - can look up
    both the present and the (already analysed) near future. */
struct AnalysisFrame
{
    float conf          = 0.0f;   // broadband gap confidence 0..1
    float bandConf[3]   { 0.0f, 0.0f, 0.0f };
    float bandEnergy[3] { 0.0f, 0.0f, 0.0f };
    float energy        = 0.0f;   // 0..1 normalised broadband energy
    float reference     = 0.0f;   // local musical reference level of the MAIN input
    float peak          = 0.0f;   // input peak over the hop (visualisation)
    float transient     = 0.0f;   // detection-lane transient strength
    float mainTransient = 0.0f;   // main-signal transient strength (protection)
    float scTransient   = 0.0f;   // sidechain transient strength (protection)
};

//==============================================================================
/** Settings pushed once per block from the audio thread. */
struct AnalysisSettings
{
    DetMode   mode        = DetMode::Transient;
    DetSource source      = DetSource::Main;
    float sensitivity01   = 0.5f;
    float gapThresh01     = 0.5f;
    float protectHoldMs   = 40.0f;
    float retrigGuardMs   = 30.0f;
    float gapReleaseMs    = 80.0f;
    float xoverLowMid     = 180.0f;
    float xoverMidHigh    = 3500.0f;
    bool  sidechainActive = false;
};

//==============================================================================
/** A single detection lane: envelopes, adaptive floor, onset and gap logic. */
class LaneDetector
{
public:
    void prepare (double sampleRateIn, int hopSizeIn);
    void reset();

    /** Recomputes envelope times - only needed when the mode changes. */
    void configure (DetMode mode);

    /** Per-sample update. Cheap: three one-poles and a peak accumulator. */
    inline void pushSample (float x) noexcept
    {
        const auto rect = std::abs (x);
        hopPeak = juce::jmax (hopPeak, rect);
        hopSum += rect * rect;
        ++hopCount;

        fast.process (rect);
        slow.process (rect);
    }

    /** Hop-rate decision step. `flux` is an optional 0..1 spectral-change cue. */
    void updateHop (const AnalysisSettings& s, float flux) noexcept;

    /** When false the lane ignores the spectral-flux cue - band lanes use this
        so that broadband activity cannot mark their own region as busy. */
    bool  useFlux           = true;

    float conf              = 0.0f;
    float energy            = 0.0f;
    /** Local musical loudness (linear peak with slow decay). Zero after the
        source has genuinely been silent for a while. */
    float referenceLevel    = 0.0f;
    float peak              = 0.0f;
    float transientStrength = 0.0f;
    bool  eventThisHop      = false;

private:
    double sampleRate = 48000.0;
    int    hopSize    = 32;
    float  hopSeconds = 0.0f;

    EnvelopeFollower fast, slow;
    float floorEnv   = 0.0f;
    float reference  = 0.0f;
    float refDecay   = 0.0f;
    float confValue  = 0.0f;

    float hopPeak = 0.0f, hopSum = 0.0f;
    int   hopCount = 0;

    float sinceEventMs  = 10000.0f;
    float sinceOnsetMs  = 10000.0f;
    float riseHoldMs    = 0.0f;
    bool  armed         = true;

    DetMode currentMode = DetMode::Transient;
};

//==============================================================================
/** Tracks short-time spectral change on the detection signal.

    This is what lets Phrase mode notice a new note that does not come with a
    level jump, and it feeds the onset score in every mode. */
class FluxAnalyser
{
public:
    void prepare (double sampleRate);
    void reset();

    /** Push one sample; returns the current (held) normalised flux value. */
    inline float push (float x) noexcept
    {
        fifo[(size_t) fifoPos] = x;
        fifoPos = (fifoPos + 1) % fftSize;

        if (++hopCounter >= hopSize)
        {
            hopCounter = 0;
            computeFrame();
        }

        return fluxValue;
    }

    float value() const noexcept { return fluxValue; }

private:
    void computeFrame() noexcept;

    static constexpr int fftOrder = 9;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 2;

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> fifo, window, scratch, prevMag;
    int fifoPos = 0, hopCounter = 0;
    float fluxValue = 0.0f;
};

//==============================================================================
/** Three-band Linkwitz-Riley splitter (phase-coherent reconstruction). */
class BandSplitter
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();
    void setCrossovers (float lowMid, float midHigh);

    /** Splits one mono sample into three bands. */
    void processMono (float x, float& low, float& mid, float& high) noexcept;

    /** Splits a stereo sample pair (channel 0/1) into three bands. */
    void processStereo (int channel, float x, float& low, float& mid, float& high) noexcept;

private:
    struct Stage
    {
        juce::dsp::LinkwitzRileyFilter<float> lowSplit, highSplit;
    };

    std::vector<Stage> stages;
    double sr = 48000.0;
    float lastLowMid = 0.0f, lastMidHigh = 0.0f;
};

//==============================================================================
class AnalysisEngine
{
public:
    static constexpr int hopSize        = 32;
    static constexpr int frameCapacity  = 8192;   // 262144 samples of history
    static constexpr int eventQueueSize = 256;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Analyses one block. `main` and `sidechain` are the mono detection
        signals; both must contain `numSamples` samples. */
    void processBlock (const float* mainMono, const float* sidechainMono,
                       int numSamples, const AnalysisSettings& settings) noexcept;

    /** Absolute sample position of the next analysed sample. */
    std::int64_t getAnalysisPosition() const noexcept { return samplePos; }

    const AnalysisFrame& frameAt (std::int64_t samplePosition) const noexcept
    {
        auto f = samplePosition / hopSize;

        if (f < 0)
            f = 0;

        return frames[(size_t) (f % frameCapacity)];
    }

    /** Position of the next detection event at or after `from`, or -1. */
    std::int64_t nextDetectionEvent (std::int64_t from) const noexcept
    {
        return detectionEvents.nextAtOrAfter (from);
    }

    std::int64_t nextProtectedEvent (std::int64_t from) const noexcept
    {
        const auto a = mainEvents.nextAtOrAfter (from);
        const auto b = scEvents.nextAtOrAfter (from);

        if (a < 0) return b;
        if (b < 0) return a;
        return juce::jmin (a, b);
    }

    std::int64_t nextMainEvent (std::int64_t from) const noexcept
    {
        return mainEvents.nextAtOrAfter (from);
    }

    std::int64_t nextSidechainEvent (std::int64_t from) const noexcept
    {
        return scEvents.nextAtOrAfter (from);
    }

    std::int64_t nextBandEvent (int band, std::int64_t from) const noexcept
    {
        return bandEvents[(size_t) juce::jlimit (0, 2, band)].nextAtOrAfter (from);
    }

    void discardEventsBefore (std::int64_t position) noexcept
    {
        detectionEvents.discardBefore (position);
        mainEvents.discardBefore (position);
        scEvents.discardBefore (position);

        for (auto& q : bandEvents)
            q.discardBefore (position);
    }

    /** Most recent broadband gap confidence (for metering). */
    float getCurrentConfidence() const noexcept { return lastConfidence; }

private:
    void writeFrame (const AnalysisSettings& s) noexcept;

    double sampleRate = 48000.0;

    LaneDetector mainLane, scLane;
    LaneDetector mainBandLanes[3], scBandLanes[3];
    BandSplitter mainSplit, scSplit;
    FluxAnalyser mainFlux, scFlux;

    std::vector<AnalysisFrame> frames;
    AnalysisFrame pending;

    EventQueue<eventQueueSize> detectionEvents, mainEvents, scEvents;
    EventQueue<eventQueueSize> bandEvents[3];

    std::int64_t samplePos = 0;
    int hopCounter = 0;

    DetMode  lastMode   = DetMode::Transient;
    DetSource lastSource = DetSource::Main;
    bool lastSidechain  = false;
    bool needsBandLanes = false;
    bool needsScLane    = false;

    float lastConfidence = 0.0f;
};

} // namespace gapfill
