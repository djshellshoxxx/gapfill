#include "LiveControl.h"

#include "../../dsp/FillEngine.h"
#include "../../PluginProcessor.h"

#include <cmath>

namespace gapfill::live
{

namespace
{
    constexpr int kPhraseBarsTable[] = { 0, 2, 4, 8, 16 };
}

LiveControl::LiveControl (juce::AudioProcessorValueTreeState& state)
    : apvts (state), sceneCtl (state)
{
    phraseBarsParam = state.getRawParameterValue ("phraseBars");        // null until the phrase group has parameters
    phraseOffsetParam = state.getRawParameterValue ("phraseOffset");
}

//==============================================================================
// Audio thread

void LiveControl::prepare (double sr, int maxChunk)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    panicCtl.prepare (sampleRate, maxChunk);
    reset();
}

void LiveControl::reset() noexcept
{
    ringCount = 0;
    sceneEngine.reset();
    panicCtl.reset();
}

void LiveControl::post (UiRequest::Kind kind, int a) noexcept
{
    UiRequest r;
    r.kind = kind;
    r.a = a;

    if (! uiQueue.push (r))
        uiDropped.fetch_add (1, std::memory_order_relaxed);
}

void LiveControl::onPanicAction (panic::Action action, FillEngine& engine) noexcept
{
    if (action == panic::Action::Panic)
    {
        // Wet ramp is the controller's; here: stop voices, cancel what was queued.
        engine.panic();
        sceneEngine.cancelPending();
        ringCount = 0;
        post (UiRequest::SceneCancel);
        post (UiRequest::PanicChanged, 1);
    }
    else if (action == panic::Action::Recover)
    {
        post (UiRequest::PanicChanged, 0);
    }
}

void LiveControl::execute (CommandType type, int arg, bool on, int sampleOffset, FillEngine& engine) noexcept
{
    switch ((int) type)
    {
        case (int) CommandType::Panic:
            if (on)
                onPanicAction (panicCtl.press (sampleOffset), engine);
            break;

        case (int) CommandType::Recover:
            if (on)
                onPanicAction (panicCtl.recover(), engine);
            break;

        // SceneGoto with a negative argument is the F-L25 "last good scene" action.
        case (int) CommandType::SceneGoto:   if (on) post (arg < 0 ? UiRequest::SceneLastGood : UiRequest::SceneGoto, arg); break;
        case (int) CommandType::SceneNext:   if (on) post (UiRequest::SceneNext); break;
        case (int) CommandType::ScenePrev:   if (on) post (UiRequest::ScenePrev); break;
        case (int) CommandType::SceneCancel:
            if (on)
            {
                sceneEngine.cancelPending();
                post (UiRequest::SceneCancel);
            }
            break;

        case (int) CommandType::PresetNext:  if (on) post (UiRequest::PresetNext); break;
        case (int) CommandType::PresetPrev:  if (on) post (UiRequest::PresetPrev); break;
        case (int) CommandType::Roulette:    if (on) post (UiRequest::Roulette); break;
        case (int) CommandType::AbSwap:      if (on) post (UiRequest::AbSwap); break;

        default:
            break;      // commands owned by other work packages are ignored here
    }
}

void LiveControl::beginBlock (const juce::MidiBuffer& midi, FillEngine& engine) noexcept
{
    commands::Event events[commands::kMaxCommandEventsPerBlock];

    // 1. A new binding table (releases anything that was held).
    if (bindingModel.publisher().consume (tableScratch))
    {
        const auto n = scanner.adoptTable (tableScratch, events, commands::kMaxCommandEventsPerBlock);

        for (int i = 0; i < n; ++i)
            execute (events[i].type, events[i].arg, events[i].on, 0, engine);
    }

    // 2. GUI / shortcut panic and recover requests.
    onPanicAction (panicCtl.beginBlock(), engine);

    // 3. Commands raised by MIDI in the PREVIOUS block, in order (stable ordering).
    const auto queued = ringCount;
    ringCount = 0;

    for (int i = 0; i < queued; ++i)
        execute (ring[i].type, ring[i].arg, ring[i].on, ring[i].sampleOffset, engine);

    // 4. This block's MIDI. Panic/Recover act immediately, the rest waits one block.
    const auto n = scanner.process (midi, events, commands::kMaxCommandEventsPerBlock);

    for (int i = 0; i < n; ++i)
    {
        const auto& e = events[i];

        if (e.type == CommandType::Panic || e.type == CommandType::Recover)
        {
            execute (e.type, e.arg, e.on, e.sampleOffset, engine);
        }
        else if (ringCount < commands::kMaxCommandEventsPerBlock)
        {
            ring[ringCount++] = e;
        }
        else
        {
            ringDropped.fetch_add (1, std::memory_order_relaxed);
        }
    }
}

void LiveControl::handleCommand (const Command& c, FillEngine& engine) noexcept
{
    // Hold-type commands use velocity 0 as "release"; every other command is a press.
    const bool holdType = c.type == CommandType::StutterStorm || ((int) c.type >= 20 && (int) c.type <= 23);
    execute (c.type, c.index, holdType ? c.velocity != 0 : true, 0, engine);
}

void LiveControl::prepareChunk (EngineSettings& s, int numSamples) noexcept
{
    scenes::TransportSnap t;
    t.valid = s.transportValid && s.samplesPerBeat > 1.0;
    t.ppq = s.ppqAtBlockStart;
    t.samplesPerBeat = s.samplesPerBeat;
    t.sampleRate = sampleRate;
    t.tsNum = s.perf.tsNum;
    t.tsDen = s.perf.tsDen;
    t.numSamples = numSamples;

    if (phraseBarsParam != nullptr)
    {
        const int idx = juce::jlimit (0, 4, (int) std::lround (phraseBarsParam->load (std::memory_order_relaxed)));
        t.phraseBars = kPhraseBarsTable[idx];
    }

    if (phraseOffsetParam != nullptr)
        t.phraseOffset = (int) std::lround (phraseOffsetParam->load (std::memory_order_relaxed));

    const auto r = sceneEngine.beginChunk (sceneCtl.publisher(), t);

    if (r.appliedId != 0)
        post (UiRequest::SceneApplied, (int) r.appliedId);

    const auto& view = sceneEngine.view();
    s.perf.sceneView = view.active ? &view : nullptr;
    s.perf.panicked = panicCtl.isPanicked();

    uiPpq.store (t.valid ? t.ppq : -1.0, std::memory_order_relaxed);
    uiTsNum.store (t.tsNum, std::memory_order_relaxed);
    uiTsDen.store (t.tsDen, std::memory_order_relaxed);
}

void LiveControl::onTransportStopped() noexcept
{
    // A stopped transport applies a pending quantised change immediately
    // (SceneEngine falls back), nothing else to do here yet.
}

//==============================================================================
// Message thread

void LiveControl::startLearn (commands::LearnTarget t)
{
    learnTicks = MidiLearnManager::learningTimeoutTicks;
    learnStatus = "Waiting for MIDI for \"" + juce::String (commands::commandName (t.cmd)) + "\"...";
    scanner.armLearn (t);
}

void LiveControl::cancelLearn()
{
    scanner.cancelLearn();
    learnTicks = 0;
    learnStatus = "Learn cancelled";
}

void LiveControl::handleUi (const UiRequest& r, GapFillAudioProcessor& proc)
{
    switch ((int) r.kind)
    {
        case (int) UiRequest::SceneApplied:  sceneCtl.onApplied ((std::uint32_t) r.a); break;
        case (int) UiRequest::SceneGoto:     sceneCtl.gotoScene (r.a); break;
        case (int) UiRequest::SceneNext:     sceneCtl.next(); break;
        case (int) UiRequest::ScenePrev:     sceneCtl.prev(); break;
        case (int) UiRequest::SceneCancel:   sceneCtl.cancel(); break;
        case (int) UiRequest::SceneLastGood: sceneCtl.gotoLastGood(); break;
        case (int) UiRequest::Roulette:      rouletteState.spin (proc); break;

        case (int) UiRequest::PresetNext:
        case (int) UiRequest::PresetPrev:
        {
            const auto n = proc.getNumPrograms();

            if (n > 0)
            {
                const auto cur = proc.getCurrentProgram();
                const auto next = r.kind == UiRequest::PresetNext ? (cur < 0 ? 0 : (cur + 1) % n)
                                                                  : (cur <= 0 ? n - 1 : cur - 1);
                proc.selectPreset (next);
            }
            break;
        }

        default:
            break;
    }
}

void LiveControl::pump (GapFillAudioProcessor& proc)
{
    UiRequest batch[8];

    for (;;)
    {
        const auto n = uiQueue.pop (batch, 8);

        if (n <= 0)
            break;

        for (int i = 0; i < n; ++i)
            handleUi (batch[i], proc);
    }

    sceneCtl.poll();

    // ---- X-1 learn ---------------------------------------------------------
    commands::Binding learned;

    while (scanner.popLearned (learned))
    {
        learnTicks = 0;

        if (learned.type == commands::MsgType::CC && midiLearn != nullptr)
        {
            const auto owner = midiLearn->getParameterIDForCC (learned.number);

            if (owner.isNotEmpty())
            {
                learnStatus = "CC " + juce::String (learned.number) + " already controls " + owner;
                continue;
            }
        }

        if (const auto existing = bindingModel.findCommand (learned.cmd, learned.arg); existing >= 0)
            bindingModel.removeAt (existing);

        int owner = -1;
        const auto result = bindingModel.add (learned, &owner);

        switch (result)
        {
            case commands::BindingModel::AddResult::Ok:
                learnStatus = juce::String (commands::commandName (learned.cmd)) + " learned";
                break;
            case commands::BindingModel::AddResult::Duplicate:
                learnStatus = "Already used by " + juce::String (commands::commandName (bindingModel.get (owner).cmd));
                break;
            case commands::BindingModel::AddResult::Full:
                learnStatus = "Command map is full (48 bindings)";
                break;
            case commands::BindingModel::AddResult::Invalid:
            default:
                learnStatus = "That message cannot be used";
                break;
        }
    }

    if (scanner.isLearning() && learnTicks > 0 && --learnTicks == 0)
    {
        scanner.cancelLearn();
        learnStatus = "Learn timed out";
    }
}

//------------------------------------------------------------------------------
PerfOptions LiveControl::getPerfOptions() const noexcept
{
    PerfOptions o;
    o.panic = panicCtl.getOptions();
    return o;
}

void LiveControl::setPerfOptions (const PerfOptions& o) noexcept
{
    panicCtl.setOptions (o.panic);
}

juce::ValueTree LiveControl::perfToValueTree() const
{
    const auto o = getPerfOptions();
    juce::ValueTree t ("Perf");
    t.setProperty ("v", 1, nullptr);
    t.setProperty ("recovery", (int) o.panic.recovery, nullptr);
    t.setProperty ("panicToggles", o.panic.panicToggles ? 1 : 0, nullptr);
    return t;
}

bool LiveControl::perfValidate (const juce::ValueTree& t)
{
    if (! t.hasType ("Perf"))
        return false;

    const auto rec = (int) t.getProperty ("recovery", 0);
    return rec >= 0 && rec <= 3;
}

void LiveControl::perfRestore (const juce::ValueTree& t)
{
    PerfOptions o;

    if (t.hasType ("Perf"))
    {
        o.panic.recovery = (panic::RecoveryMode) juce::jlimit (0, 3, (int) t.getProperty ("recovery", 0));
        o.panic.panicToggles = (int) t.getProperty ("panicToggles", 1) != 0;
    }

    setPerfOptions (o);
}

} // namespace gapfill::live
