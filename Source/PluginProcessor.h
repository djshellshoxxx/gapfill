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
#include "features/live/LiveControl.h"
#include "features/shaping/ShapingRuntime.h"
#include "features/recipes/RecipeServices.h"
#include "features/fx/FxChain.h"
#include "features/fx/FxUiState.h"
#include "features/explain/ExplainState.h"
#include "features/context/ContextState.h"
#include "features/ui/UiState.h"
#include "features/scdrive/ScDrive.h"
#include "features/midiout/MidiOut.h"
#include "features/quality/Quality.h"
#include "features/coop/Coop.h"

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
    // MIDI input is accepted for CC-based parameter control (MIDI learn) and the command map.
    // MIDI OUTPUT (gap / fill notes, F-L11) is declared only when the build option GAPFILL_MIDI_OUT is on.
    bool acceptsMidi() const override                    { return true; }
    bool producesMidi() const override                   { return midiout::kDeclared; }
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
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept { return apvts; }
    MeterState& getMeterState() noexcept                            { return meters; }
    VisualisationFifo& getVisualisationFifo() noexcept              { return viz; }
    MidiLearnManager& getMidiLearnManager() noexcept                { return midiLearn; }

    /** WP6 live control hub (panic, scenes, MIDI command map). */
    live::LiveControl& getLiveControl() noexcept                    { return liveControl; }
    const live::LiveControl& getLiveControl() const noexcept        { return liveControl; }
    /** WP5 runtime state: tempo memory (E-2), effective creative values (C-0). */
    ShapingRuntime& getShaping() noexcept                           { return shaping; }
    const ShapingRuntime& getShaping() const noexcept               { return shaping; }
    const EffectiveCreative& getEffectiveCreative() const noexcept  { return shaping.effective; }
    /** WP3 Explain & assist: health codes, telemetry, fix-it, timeline, event recorder. */
    const context::ContextState& getContextState() const noexcept   { return contextState; }
    context::ContextState& getContextState() noexcept               { return contextState; }
    explain::ExplainState& getExplainState() noexcept               { return explainState; }
    const explain::ExplainState& getExplainState() const noexcept   { return explainState; }

    /** WP10 routing & system. */
    const scdrive::SidechainOnsetTracker& getScTracker() const noexcept   { return scTracker; }       // F-L13
    midiout::MidiOutEmitter& getMidiOut() noexcept                         { return midiOut; }         // F-L11
    /** F-L11 Options value: shifts the note time-stamps by -100..+100 ms (hosts that do not delay MIDI by PDC). */
    void setMidiOutOffsetMs (double ms) noexcept    { midiOutOffsetMs.store (juce::jlimit (-(double) midiout::kMaxOffsetMs, (double) midiout::kMaxOffsetMs, ms), std::memory_order_relaxed); }
    double getMidiOutOffsetMs() const noexcept      { return midiOutOffsetMs.load (std::memory_order_relaxed); }
    const quality::QualityController& getQualityController() const noexcept { return qualityCtl; }    // F-D03
    int getAppliedQualityLevel() const noexcept     { return appliedQuality.load (std::memory_order_relaxed); }
    const coop::CoopMember& getCoop() const noexcept                       { return coopMember; }      // F-X06

    /** WP11 UI shell state: status board, Simple/Advanced view mode (message thread). */
    ui::UiState& getUiState() noexcept                              { return uiState; }
    const ui::UiState& getUiState() const noexcept                  { return uiState; }

    //==========================================================================
    // Cross-thread queues (docs/dev/02 F0-4)

    /** Message thread only (single producer). Returns false if the queue is full. */
    bool postCommand (const Command& command) noexcept   { return commands.push (command); }

    /** Message thread. Moves audio-thread fill events into the history and the
        outcome log; also called by the 15 Hz timer. Returns how many were drained. */
    int drainFillEvents();

    const RecipeHistory& getRecipeHistory() const noexcept   { return recipeHistory; }
    RecipeHistory& getRecipeHistory() noexcept               { return recipeHistory; }   // state restore (message thread)

    /** WP4 facade: audition trigger, lock mirror, history, export, A/B match. */
    recipes::RecipeServices& getRecipes() noexcept               { return recipeServices; }
    const recipes::RecipeServices& getRecipes() const noexcept   { return recipeServices; }
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
    fx::FxChain& getFxChain() noexcept { return fxChain; }
    const fx::FxMeters& getFxMeters() const noexcept { return fxChain.getMeters(); }
    fx::FxUiState& getFxUiState() noexcept { return fxUi; }
    const fx::FxUiState& getFxUiState() const noexcept { return fxUi; }
    const AnalysisEngine& getAnalysis() const noexcept { return analysis; }
    int getActiveLatencySamples() const noexcept { return meters.latencySamples.load(); }
    std::int64_t getSourceHistoryWritePosition() const noexcept { return history.getWritePosition(); }
   #ifdef GAPFILL_UNIT_TESTS
    void flushLatencyReportForTests() { timerCallback(); }
    int getTransportJumpCountForTests() const noexcept { return transportJumpCountForTests; }
   #endif

private:
    void timerCallback() override;
    void drainCommands() noexcept;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, bool bypassed);
    EngineSettings gatherEngineSettings();
    AnalysisSettings gatherAnalysisSettings (bool sidechainActive) const;
    void updateTempoInfo (EngineSettings& settings, int startSample, int numSamples);
    bool isParameterStateValid (const juce::ValueTree& state) const;
    void snapParametersToState();
    void panicWet() noexcept { engine.panic(); fxChain.fadeOut ((int) (currentSampleRate * 0.004)); midiOut.requestFlush(); }
    void clearWetBus (juce::AudioBuffer<float>&) noexcept;          // F-L12
    /** Input, Output, Sidechain, plus the optional "Wet" output (F-L12; not for Audio Unit, see wetbus::isDeclared).
        A static member because BusesProperties is protected in juce::AudioProcessor. */
    static BusesProperties makeBuses();
    midiout::Settings gatherMidiOutSettings() const noexcept;       // F-L11

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    ParamCache params;
    juce::AudioProcessorParameter* bypassParameter = nullptr;

    AnalysisEngine analysis;
    SourceHistory  history;
    FillEngine     engine;
    fx::FxChain    fxChain;       // WP8: bus effect chain (voice cards live in the voices)
    fx::FxUiState  fxUi;          // WP8: <Fx> state subtree (fold state, remembered amounts)

    MidiLearnManager midiLearn;
    live::LiveControl liveControl { apvts };
    recipes::RecipeServices recipeServices;

    // WP10 routing & system (docs/architecture/MODULES.md)
    scdrive::SidechainOnsetTracker scTracker;        // F-L13: fed only while a gate / snap is set and the sidechain is connected
    midiout::MidiOutEmitter midiOut;                 // F-L11
    std::atomic<double> midiOutOffsetMs { 0.0 };
    quality::QualityController qualityCtl;           // F-D03
    std::atomic<int> appliedQuality { 0 };
    coop::CoopMember coopMember;                     // F-X06: claims a registry slot only while coopGroup != Off

    MeterState meters;
    VisualisationFifo viz;
    ShapingRuntime shaping;

    CommandQueue    commands;        // message -> audio, drained once per block
    FillEventQueue  fillEvents;      // audio -> message, drained by the timer
    RecipeHistory   recipeHistory;   // message thread only (survives the editor)
    GapOutcomeLog   outcomeLog;      // message thread only
    // WP7: musical-context trackers. Heap-allocated so the processor stays under the stack budget (TestStackBudget).
    std::unique_ptr<context::ContextState> contextStateOwner { std::make_unique<context::ContextState>() };
    context::ContextState& contextState { *contextStateOwner };
    explain::ExplainState explainState;   // WP3; attached to apvts in the constructor
    ui::UiState uiState;                  // WP11: status board, view mode

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
    int hostBlockSamples = 0; // total frames in the current DAW callback (not its current chunk)

    HighPass2 lowProtect[2];
    float lastLowProtectHz = -1.0f;

    BandSplitter soloSplitter;

    juce::SmoothedValue<float> dryGain, wetGain, outGain, centreProtect, widthAmount, bypassMix;

    double lastPpqPosition = 0.0; // projected PPQ at the next DAW callback
    bool   lastTransportPlaying = false;
    bool   lastTransportPositionValid = false;
   #ifdef GAPFILL_UNIT_TESTS
    int transportJumpCountForTests = 0;
   #endif

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
