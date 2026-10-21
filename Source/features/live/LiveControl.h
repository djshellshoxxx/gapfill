#pragma once

// WP6 live control hub (docs/dev/12 X-1, N-USE-a, F-L07, N-VAL, ...).
//
// One object owned by the processor. It composes the live-control modules and
// is the ONLY thing the processor talks to:
//
//   audio thread   beginBlock()     panic requests, MIDI command scan (X-1)
//                  handleCommand()  commands from the message-thread queue
//                  prepareChunk()   scene quantiser, panicked flag -> EngineSettings
//                  wetGains()       fixed panic ramp applied to the wet bus
//   message thread pump()           15 Hz: UI requests, scene commit/poll, learn
//
// Cross-thread paths: TripleBuffer (tables, overlays), atomics (panic), and one
// bounded SPSC queue audio -> message (UiRequest). Nothing here allocates,
// locks or does IO on the audio thread.

#include "../battle/Battle.h"
#include "../commands/CommandMap.h"
#include "../commands/ControllerProfile.h"
#include "../macros/MacroModel.h"
#include "../panic/Panic.h"
#include "../scenes/SceneController.h"
#include "Roulette.h"
#include "../../dsp/EngineSettings.h"
#include "../effective/Effective.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace gapfill
{

class FillEngine;
class GapFillAudioProcessor;
class MidiLearnManager;

namespace live
{

/** Audio -> message thread requests (things only the message thread may do). */
struct UiRequest
{
    enum Kind : std::uint8_t
    {
        None = 0, PresetNext, PresetPrev, Roulette, AbSwap,
        SceneGoto, SceneNext, ScenePrev, SceneCancel, SceneApplied, SceneLastGood,
        PanicChanged
    };

    Kind kind = None;
    std::int32_t a = 0;
};

/** Options kept in the <Perf> subtree (not automatable). */
struct PerfOptions
{
    panic::Options panic;
};

class LiveControl
{
public:
    explicit LiveControl (juce::AudioProcessorValueTreeState&);

    //==========================================================================
    // Message thread.
    commands::BindingModel& bindings() noexcept          { return bindingModel; }
    const commands::BindingModel& bindings() const noexcept { return bindingModel; }
    scenes::SceneController& scenes() noexcept           { return sceneCtl; }
    const scenes::SceneController& scenes() const noexcept { return sceneCtl; }
    battle::BattleModel& battles() noexcept              { return battleModel; }
    battle::Status getBattleStatus() const noexcept      { return (battle::Status) uiBattle.load (std::memory_order_relaxed); }
    macros::MacroModel& macroMappings() noexcept         { return macroModel; }
    const macros::MacroModel& macroMappings() const noexcept { return macroModel; }
    GlitchRoulette& roulette() noexcept                  { return rouletteState; }
    panic::PanicController& panic() noexcept             { return panicCtl; }
    const panic::PanicController& panic() const noexcept { return panicCtl; }

    /** 15 Hz from the processor timer. */
    void pump (GapFillAudioProcessor&);

    //--- X-1 learn -----------------------------------------------------------
    void startLearn (commands::LearnTarget);
    void cancelLearn();
    bool isLearning() const noexcept                     { return scanner.isLearning(); }
    int  getLearnTicksRemaining() const noexcept         { return learnTicks; }
    const juce::String& getLearnStatus() const noexcept  { return learnStatus; }
    void setMidiLearn (MidiLearnManager* m) noexcept     { midiLearn = m; }

    //--- F-L24 controller profiles and LED feedback ---------------------------
    /** Installs a profile's bindings (Replace clears first, otherwise existing bindings win) and its
        feedback rules. `message` explains skipped bindings. */
    bool applyProfile (const commands::Profile&, bool replace, juce::String& message);

    /** Chooses the MIDI output device that mirrors LED state. Standalone only: elsewhere the plugin
        emits no MIDI and the device is ignored. An empty name switches feedback off. */
    void setFeedbackDevice (const juce::String& deviceName);

    /** Test seam: install any sink (production code uses setFeedbackDevice). */
    void setFeedbackSink (std::unique_ptr<commands::MidiSink>);

    /** Re-resolves the active profile's feedback rules and the output device after `<Cmd>` was restored. */
    void refreshFeedbackAfterRestore();

    static bool feedbackAvailableInThisHost() noexcept;
    const juce::String& getFeedbackStatus() const noexcept     { return feedbackStatus; }
    commands::FeedbackEngine& feedbackEngine() noexcept        { return feedback; }

    //--- options -------------------------------------------------------------
    PerfOptions getPerfOptions() const noexcept;
    void setPerfOptions (const PerfOptions&) noexcept;

    juce::ValueTree perfToValueTree() const;
    static bool perfValidate (const juce::ValueTree&);
    void perfRestore (const juce::ValueTree&);

    //==========================================================================
    // Audio thread.
    void prepare (double sampleRate, int maxChunk);
    void reset() noexcept;

    /** Top of processBlock, after MidiLearnManager::processMidi. */
    void beginBlock (const juce::MidiBuffer&, FillEngine&) noexcept;

    /** One command drained from the processor's message-thread queue. */
    void handleCommand (const Command&, FillEngine&) noexcept;

    /** Block stage inputs: adopts a newly published macro table and attaches the runtime. */
    void fillModulationInputs (Effective::ModulationInputs&, int seed) noexcept;

    /** Latest effective NORMALISED value of a macro target (any thread, for the UI). */
    float getMacroEffective01 (int target) const noexcept
    {
        return target >= 0 && target < macros::kNumTargets ? uiEffective[target].load (std::memory_order_relaxed) : 0.0f;
    }

    /** Per processed chunk, after the transport fields of `s` are filled. */
    void prepareChunk (EngineSettings& s, int numSamples) noexcept;

    /** Wet-bus gains for the next `n` samples (sequential chunks of one block). */
    const float* wetGains (int n) noexcept               { return panicCtl.nextGains (n); }

    /** Bypass / transport stop / jump: cancels pending scene changes. */
    void onTransportStopped() noexcept;

    std::uint32_t getDroppedUiRequests() const noexcept  { return uiDropped.load (std::memory_order_relaxed); }
    std::uint32_t getDroppedCommandEvents() const noexcept { return ringDropped.load (std::memory_order_relaxed); }

    /** Latest host position as seen by the audio thread (UI countdown). */
    double getUiPpq() const noexcept                     { return uiPpq.load (std::memory_order_relaxed); }
    int    getUiTimeSigNum() const noexcept              { return uiTsNum.load (std::memory_order_relaxed); }
    int    getUiTimeSigDen() const noexcept              { return uiTsDen.load (std::memory_order_relaxed); }

private:
    void execute (CommandType, int arg, bool on, int sampleOffset, FillEngine&) noexcept;
    void onPanicAction (panic::Action, FillEngine&) noexcept;
    void post (UiRequest::Kind, int a = 0) noexcept;
    void handleUi (const UiRequest&, GapFillAudioProcessor&);

    juce::AudioProcessorValueTreeState& apvts;
    MidiLearnManager* midiLearn = nullptr;

    commands::BindingModel bindingModel;
    commands::Scanner scanner;
    scenes::SceneController sceneCtl;
    scenes::SceneEngine sceneEngine;
    panic::PanicController panicCtl;
    macros::MacroModel macroModel;
    battle::BattleModel battleModel;
    battle::BattleRuntime battleRuntime;
    std::atomic<int> uiBattle { 0 };
    macros::MacroRuntime macroRuntime;
    std::atomic<float> uiEffective[macros::kNumTargets] {};
    GlitchRoulette rouletteState { juce::Random::getSystemRandom() };

    // audio-thread-local ring of MIDI-originated commands (drained next block)
    commands::Event ring[commands::kMaxCommandEventsPerBlock] {};
    int ringCount = 0;
    std::atomic<std::uint32_t> ringDropped { 0 };

    SpscQueue<UiRequest, 32> uiQueue;
    std::atomic<std::uint32_t> uiDropped { 0 };

    commands::BindingTable tableScratch;

    std::atomic<float>* phraseBarsParam = nullptr;     // optional (WP5 group)
    std::atomic<float>* phraseOffsetParam = nullptr;
    double sampleRate = 48000.0;

    std::atomic<double> uiPpq { 0.0 };
    std::atomic<int> uiTsNum { 0 }, uiTsDen { 0 };

    commands::FeedbackEngine feedback;
    juce::String feedbackStatus;
    int pumpTicks = 0;

    int learnTicks = 0;
    juce::String learnStatus;
};

} // namespace live
} // namespace gapfill
