#pragma once

// Integration point of the "Explain & assist" package (WP3): E-3 reasons, F-S01
// telemetry, N-USE-b health, N-EASY fix-it, F-M03 timeline, F-D02 event recorder.
//
// The processor owns exactly ONE ExplainState and talks to it through four
// narrow calls (see docs/architecture/MODULES.md):
//   audio thread    beginBlock()/endBlock()   CPU estimate (two clock reads)
//   audio thread    onChunk()                 health codes + event recording
//   audio thread    onCommand()               event recording
//   message thread  onFillEvent(), timerTick()  timeline, health tracker, fix-it
// Everything else (GUI, tests) reads the members below. Nothing in here makes
// a fill decision.

#include "../events/EventRecorder.h"
#include "../fixit/Fixit.h"
#include "../health/Health.h"
#include "../telemetry/Telemetry.h"
#include "../timeline/Model.h"

#include <juce_data_structures/juce_data_structures.h>

#include <functional>

namespace juce { class AudioProcessorValueTreeState; }

namespace gapfill
{

class GapFillAudioProcessor;

namespace explain
{

/** What the processor observed for one processed chunk. Plain data. */
struct ChunkInfo
{
    int numSamples = 0;
    double sampleRate = 48000.0;
    bool blockStart = true;             ///< first chunk of a host block
    bool transportValid = false;
    double ppq = 0.0;                   ///< position at the START of the host block
    double bpm = 120.0;
    int tsNum = 0, tsDen = 0;
    bool tempoFeatureOn = false;
    bool scRequested = false;           ///< Detector Source is not Main
    bool scBusEnabled = false;
    float scPeak = 0.0f;                ///< linear peak of the sidechain in this chunk
    bool bypassed = false;
    std::uint32_t droppedTotal = 0;     ///< FillEventQueue::getDroppedCount()
};

float peakOf (const float* samples, int n) noexcept;

class ExplainState
{
public:
    ExplainState();

    /** Constructor-time (message thread): remember the owner and hook the
        parameter atomics into the event recorder. */
    void attach (GapFillAudioProcessor&, juce::AudioProcessorValueTreeState&);

    //--- audio thread ----------------------------------------------------------
    std::int64_t beginBlock() const noexcept                              { return cpu.begin(); }
    void endBlock (std::int64_t token, int numSamples, double rate) noexcept
    {
        cpu.end (token, numSamples, rate);
    }

    void onChunk (const ChunkInfo&) noexcept;
    void onCommand (const Command& c) noexcept
    {
        recorder.onCommand (c);

        // Command 29 (EventRecordToggle): starting a recording allocates, so the
        // audio thread only raises a flag and the message-thread timer acts on it.
        if (c.type == CommandType::EventRecordToggle)
            recordToggleRequested.store (true, std::memory_order_relaxed);
    }

    //--- message thread --------------------------------------------------------
    /** One event drained from the audio -> message queue. */
    void onFillEvent (const FillEvent&) noexcept;

    /** After a drain: report events the queues dropped (marks the timeline incomplete). */
    void noteQueueDrops (std::uint32_t droppedOutcomes, std::uint32_t droppedOptional) noexcept;

    /** 15 Hz (processor timer). */
    void timerTick (std::uint32_t nowMs);

    /** Fix-it actions for the GUI. */
    bool applyFixit (bool confirmed);
    bool undoFixit();
    void dismissFixit();
    void muteFixitRule (fixit::RuleId);
    void muteHealthCode (health::HealthCode, bool shouldMute) noexcept;

    std::atomic<bool> recordToggleRequested { false };

    /** "Open Options at Tempo when host provides none": set by the editor. */
    std::function<void()> openOptions;

    //--- shared, persisted in the <Health> subtree ------------------------------
    std::atomic<bool> showSuggestions { true };
    std::atomic<bool> legendCollapsed { false };
    std::atomic<std::uint32_t> mutedHealth { 0 };      ///< bit per health::HealthCode
    std::atomic<std::uint32_t> mutedFixit { 0 };       ///< bit per fixit::RuleId
    std::atomic<int> timelineZoomSeconds { 8 };        ///< 4, 8 or 16

    //--- owned parts -----------------------------------------------------------
    telemetry::CpuMeter cpu;                           // audio writes
    health::HealthMonitor healthMonitor;               // audio writes
    events::EventRecorder recorder;                    // audio writes while recording

    health::HealthTracker healthTracker;               // message thread
    fixit::Controller fixitController;                 // message thread
    timeline::TimelineModel timelineModel;             // message thread

    /** Estimated fallback tempo shown in health texts (E-2 will provide the setting). */
    std::atomic<double> fallbackBpm { 120.0 };

    /** Latest sample rate / latency, for the telemetry text. */
    std::atomic<double> sampleRate { 48000.0 };

private:
    GapFillAudioProcessor* owner = nullptr;
    juce::AudioProcessorValueTreeState* apvts = nullptr;

    // audio-thread-only transport memory for jump detection
    bool lastTransportValid = false;
    double lastPpq = 0.0;
    int lastChunkSamples = 0;
    std::uint32_t lastDropped = 0;
    std::uint32_t lastOverruns = 0;

    // message-thread tick bookkeeping
    int fixitDivider = 0;
    std::uint32_t lastDroppedOutcomes = 0, lastDroppedOptional = 0;
    std::int64_t newestEventPos = 0;
};

/** RAII wrapper for the CPU estimate: two clock reads per processBlock. */
struct BlockTimer
{
    BlockTimer (ExplainState& s, int numSamples, double sampleRate) noexcept
        : state (s), samples (numSamples), rate (sampleRate), token (s.beginBlock()) {}
    ~BlockTimer() { state.endBlock (token, samples, rate); }

    ExplainState& state;
    int samples;
    double rate;
    std::int64_t token;
};

//==============================================================================
// <Health> state subtree (docs/dev/12 section 2.3, 1 KiB): muted codes, muted
// fix-it rules, the "Show suggestions" option, the legend fold state and the
// timeline zoom. Rows for state::reservedSubtrees().
bool validateSubtree (const juce::ValueTree&);
void applySubtree (GapFillAudioProcessor&, const juce::ValueTree&);
juce::ValueTree serialiseSubtree (const GapFillAudioProcessor&);
void resetSubtree (GapFillAudioProcessor&);

} // namespace explain
} // namespace gapfill
