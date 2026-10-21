// GapFill
// Copyright © 2026 Sheldon Davidson.
// Licensed under AGPL-3.0-or-later. See LICENSE.
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "Presets.h"
#include "MidiLearn.h"
#include "dsp/AnalysisEngine.h"
#include "dsp/EngineEvents.h"
#include "dsp/EventLogs.h"
#include "dsp/FillEngine.h"
#include "dsp/VisualisationState.h"
#include "features/shaping/ShapingRuntime.h"

namespace gapfill
{

//==============================================================================
class GapFillAudioProcessor  : public juce::AudioProcessor,
                               private juce::Timer,
                               private juce::AudioProcessorValueTreeState::Listener
{
public:
    GapFillAudioProcessor();
    ~GapFillAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                      { return true; }

    const juce::String getName() const override          { return "GapFill"; }
    // MIDI is accepted for CC-based parameter control (MIDI learn) only.
    bool acceptsMidi() const override                    { return true; }
    bool producesMidi() const override                   { return false; }
    bool isMidiEffect() const override                   { return false; }
    double getTailLengthSeconds() const override         { return 2.5; }

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    //==========================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override
    {
        const auto maxProgram = juce::jmax (0, getNumPrograms() - 1);
        return juce::jlimit (0, maxProgram, currentProgram.load (std::memory_order_relaxed));
    }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Validates and atomically applies a bare APVTS parameter tree.
        Unknown parameters are ignored for forward compatibility; malformed
        values for known parameters reject the whole state. */
    bool applyParameterState (const juce::ValueTree& state);

    static constexpr std::int64_t maxPresetFileBytes = 1024 * 1024;

    /** Saves a portable .gapfill parameter tree. Returns false and a
        user-facing reason on failure. */
    bool saveParameterStateToFile (const juce::File& file, juce::String& error);

    /** Loads a portable .gapfill parameter tree with bounded file size and
        atomic validation. Returns false and a user-facing reason on failure. */
    bool loadParameterStateFromFile (const juce::File& file, juce::String& error);

    //==========================================================================
    // Editor-facing helpers (message thread only)
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
    MeterState& getMeterState() noexcept                            { return meters; }
    VisualisationFifo& getVisualisationFifo() noexcept              { return viz; }
    MidiLearnManager& getMidiLearnManager() noexcept                { return midiLearn; }

    /** WP5 runtime state: tempo memory (E-2), effective creative values (C-0). */
    ShapingRuntime& getShaping() noexcept                           { return shaping; }
    const ShapingRuntime& getShaping() const noexcept               { return shaping; }
    const EffectiveCreative& getEffectiveCreative() const noexcept  { return shaping.effective; }

    //==========================================================================
    // Cross-thread queues (docs/dev/02 F0-4)

    /** Message thread only (single producer). Returns false if the queue is full. */
    bool postCommand (const Command& command) noexcept   { return commands.push (command); }

    /** Message thread. Moves audio-thread fill events into the history and the
        outcome log; also called by the 15 Hz timer. Returns how many were drained. */
    int drainFillEvents();

    const RecipeHistory& getRecipeHistory() const noexcept   { return recipeHistory; }
    const GapOutcomeLog& getGapOutcomeLog() const noexcept   { return outcomeLog; }
    std::uint32_t getDroppedFillEvents() const noexcept      { return fillEvents.getDroppedCount(); }

    void resetAllParameters();
    /** Randomises creative (or, with `everything`, a wider) parameter set.
        Always produces a different result from the previous press. */
    void randomise (bool everything);

    /** Applies a curated "Surprise Me" profile (see surpriseParameters) and
        returns its name. Never repeats the previous profile. */
    juce::String surprise();

    //==========================================================================
    // Undo history for whole-sound actions (Random, Surprise, Reset, preset
    // changes and file loads). Message thread only.
    static constexpr int maxUndoSteps = 32;

    /** Records the current sound so the next big action can be undone. */
    void pushUndoSnapshot();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    int  getUndoDepth() const noexcept { return (int) undoStack.size(); }
    /** Restores the sound from before the most recent big action. */
    bool undo();

    juce::String getCurrentPresetName() const;
    void selectPreset (int index);

    /** Options-page settings. These are UI preferences rather than
        parameters, so they live in the plugin state but are not automatable. */
    bool isTooltipsEnabled() const noexcept
    {
        return tooltipsEnabled.load (std::memory_order_relaxed);
    }
    void setTooltipsEnabled (bool shouldBeOn) noexcept
    {
        tooltipsEnabled.store (shouldBeOn, std::memory_order_relaxed);
    }

    bool isReducedMotionEnabled() const noexcept
    {
        return reducedMotionEnabled.load (std::memory_order_relaxed);
    }
    void setReducedMotionEnabled (bool shouldReduce) noexcept
    {
        reducedMotionEnabled.store (shouldReduce, std::memory_order_relaxed);
    }

    bool isKeyboardNavigationEnabled() const noexcept
    {
        return keyboardNavigationEnabled.load (std::memory_order_relaxed);
    }
    void setKeyboardNavigationEnabled (bool shouldEnable) noexcept
    {
        keyboardNavigationEnabled.store (shouldEnable, std::memory_order_relaxed);
    }

    /** Editor size persistence. */
    juce::Point<int> getSavedEditorSize() const noexcept
    {
        return { savedEditorWidth.load (std::memory_order_relaxed),
                 savedEditorHeight.load (std::memory_order_relaxed) };
    }
    void setSavedEditorSize (juce::Point<int> size) noexcept
    {
        savedEditorWidth.store (size.x, std::memory_order_relaxed);
        savedEditorHeight.store (size.y, std::memory_order_relaxed);
    }

    //==========================================================================
    // Test-facing accessors
    const FillEngine& getFillEngine() const noexcept  { return engine; }
    const AnalysisEngine& getAnalysis() const noexcept { return analysis; }
    int getActiveLatencySamples() const noexcept { return meters.latencySamples.load(); }
    std::int64_t getSourceHistoryWritePosition() const noexcept { return history.getWritePosition(); }
   #ifdef GAPFILL_UNIT_TESTS
    void flushLatencyReportForTests() { timerCallback(); }
   #endif

private:
    void timerCallback() override;
    void drainCommands() noexcept;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, bool bypassed);
    EngineSettings gatherEngineSettings() const;
    AnalysisSettings gatherAnalysisSettings (bool sidechainActive) const;
    void updateTempoInfo (EngineSettings& settings, int startSample, int numSamples);
    bool isParameterStateValid (const juce::ValueTree& state) const;
    void snapParametersToState();

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    ParamCache params;
    juce::AudioProcessorParameter* bypassParameter = nullptr;

    AnalysisEngine analysis;
    SourceHistory  history;
    FillEngine     engine;

    MidiLearnManager midiLearn;

    MeterState meters;
    VisualisationFifo viz;
    ShapingRuntime shaping;

    CommandQueue    commands;        // message -> audio, drained once per block
    FillEventQueue  fillEvents;      // audio -> message, drained by the timer
    RecipeHistory   recipeHistory;   // message thread only (survives the editor)
    GapOutcomeLog   outcomeLog;      // message thread only

    // --- buffers (all sized in prepareToPlay) ---
    juce::AudioBuffer<float> mainBuffer, wetBuffer, delayRing;
    std::vector<float> monoMain, monoSide;

    int   delayRingSize = 0;
    int   delayWrite = 0;
    int   currentLatency = 0;
    int   targetLatency = 0;
    int   latencyCrossfade = 0;
    int   latencyCrossfadeLength = 0;
    int   pendingLatencyFrom = 0;

    int   maxLookaheadSamples = 0;
    int   preparedBlockSize = 512;
    double currentSampleRate = 48000.0;
    int   mainChannels = 2;

    std::int64_t samplesProcessed = 0;

    HighPass2 lowProtect[2];
    float lastLowProtectHz = -1.0f;

    BandSplitter soloSplitter;

    juce::SmoothedValue<float> dryGain, wetGain, outGain, centreProtect, widthAmount, bypassMix;

    double lastPpqPosition = 0.0;
    bool   lastTransportPlaying = false;

    float meterIn = 0.0f, meterOut = 0.0f, meterWet = 0.0f;

    int   sidechainSilentSamples = 0;
    bool  sidechainBusEnabled = false;
    bool  sidechainHasSignal = false;
    float lastSoloXoverLM = -1.0f, lastSoloXoverMH = -1.0f;

    std::atomic<int> currentProgram { 0 };
    std::atomic<bool> programChangeInProgress { false };
    std::atomic<int> savedEditorWidth { 1100 };
    std::atomic<int> savedEditorHeight { 700 };
    std::atomic<bool> tooltipsEnabled { true };
    std::atomic<bool> reducedMotionEnabled { false };
    std::atomic<bool> keyboardNavigationEnabled { false };

    juce::Random randomSource;
    int lastSurpriseProfile = -1;
    std::vector<juce::ValueTree> undoStack;
    std::atomic<int> latencyToReport { 0 };
    std::atomic<bool> prepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapFillAudioProcessor)
};

} // namespace gapfill
