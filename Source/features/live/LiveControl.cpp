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

    scenes::initTables();
    scenes::initOverlayTargets();
    macros::initTargets();

    static const char* const kHoldIds[] = { "fm10_stutter", "fm10_freeze", "fm10_reverse", "fm10_tape" };

    for (int i = 0; i < 4; ++i)
        fm10Bool[i] = state.getRawParameterValue (kHoldIds[i]);

    fm10Mode = state.getRawParameterValue ("fm10_mode");
    fm10Level = state.getRawParameterValue ("fm10_level");
    fm10DryDuck = state.getRawParameterValue ("fm10_dryDuck");
    fm10Protect = state.getRawParameterValue ("fm10_protect");
    fm10Timeout = state.getRawParameterValue ("fm10_timeout");
    holdDry.setCurrentAndTargetValue (1.0f);
    bool available[macros::kNumTargets] {};

    for (int i = 0; i < macros::kNumTargets; ++i)
        available[i] = state.getParameter (macros::targetInfo (i).id) != nullptr;

    macroModel.setTargetAvailability (available);
    macroRuntime.cfg = macros::defaultConfig();
}

//==============================================================================
// Audio thread

void LiveControl::prepare (double sr, int maxChunk)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    holdDry.reset (sampleRate, 0.010);                    // 10 ms
    holdDry.setCurrentAndTargetValue (1.0f);
    holdDryGains.assign ((size_t) std::max (1, maxChunk), 1.0f);
    holdDryActive = false;
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

void LiveControl::emit (FillEventType type, std::int64_t pos) noexcept
{
    if (eventQueue == nullptr)
        return;

    FillEvent e;
    e.type = type;
    e.pos = pos;
    eventQueue->push (e);
}

void LiveControl::onPanicAction (panic::Action action, FillEngine& engine) noexcept
{
    if (action == panic::Action::Panic)
    {
        // Wet ramp is the controller's; here: stop voices, cancel what was queued.
        if (panicHook != nullptr)
            panicHook (panicContext);
        else
            engine.panic();

        sceneEngine.cancelPending();
        fillNowRequested = fillNowPending = false;      // panic cancels queued actions
        hold.panic();                                   // and releases every hold
        engine.panicHolds();
        ringCount = 0;
        post (UiRequest::SceneCancel);
        post (UiRequest::PanicChanged, 1);
        emit (FillEventType::Panic, blockPos);
    }
    else if (action == panic::Action::Recover)
    {
        post (UiRequest::PanicChanged, 0);
        emit (FillEventType::Recover, blockPos);
    }
}

void LiveControl::execute (CommandType type, int arg, bool on, int sampleOffset, FillEngine& engine, momentary::Source src) noexcept
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

        case (int) CommandType::MomentaryStutter: hold.setLevel (momentary::Effect::Stutter, src, on); break;
        case (int) CommandType::MomentaryFreeze:  hold.setLevel (momentary::Effect::Freeze, src, on); break;
        case (int) CommandType::MomentaryReverse: hold.setLevel (momentary::Effect::Reverse, src, on); break;
        case (int) CommandType::MomentaryTape:    hold.setLevel (momentary::Effect::TapeStop, src, on); break;
        case (int) CommandType::StutterStorm:     hold.setLevel (momentary::Effect::Storm, src, on); break;

        case (int) CommandType::FillNow:   if (on) fillNowRequested = true; break;

        case (int) CommandType::PresetNext:  if (on) post (UiRequest::PresetNext); break;
        case (int) CommandType::PresetPrev:  if (on) post (UiRequest::PresetPrev); break;
        case (int) CommandType::Roulette:    if (on) post (UiRequest::Roulette); break;
        case (int) CommandType::AbSwap:      if (on) post (UiRequest::AbSwap); break;

        default:
            break;      // commands owned by other work packages are ignored here
    }
}

void LiveControl::beginBlock (const juce::MidiBuffer& midi, FillEngine& engine, std::int64_t outputPos) noexcept
{
    blockPos = outputPos;
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
    execute (c.type, c.index, holdType ? c.velocity != 0 : true, 0, engine, momentary::Source::Gui);
}

void LiveControl::fillModulationInputs (Effective::ModulationInputs& in, int seed) noexcept
{
    macros::MacroConfig fresh;

    if (macroModel.publisher().consume (fresh))
    {
        macroRuntime.cfg = fresh;

        for (auto& h : macroRuntime.hasSmoothed)
            h = false;
    }

    macroRuntime.seed = seed;
    in.macros.runtime = &macroRuntime;
}

void LiveControl::prepareChunk (EngineSettings& s, int numSamples, std::int64_t outputPos) noexcept
{
    // F-M10: automation / CC-learn path (Source::Param) and the mode / timeout / level options.
    if (fm10Mode != nullptr)
    {
        hold.setLatch (fm10Mode->load (std::memory_order_relaxed) > 0.5f);

        for (int i = 0; i < 4; ++i)
            if (fm10Bool[i] != nullptr)
                hold.setLevel ((momentary::Effect) i, momentary::Source::Param, fm10Bool[i]->load (std::memory_order_relaxed) > 0.5f);

        const auto timeoutSeconds = juce::jlimit (2.0f, 60.0f, fm10Timeout->load (std::memory_order_relaxed));
        hold.tick (numSamples, (std::int64_t) ((double) timeoutSeconds * sampleRate));
        s.perf.holdLevel01 = juce::jlimit (0.0f, 1.0f, fm10Level->load (std::memory_order_relaxed) * 0.01f);
        s.perf.holdRespectProtection = fm10Protect->load (std::memory_order_relaxed) > 0.5f;
    }

    s.perf.holdTop = hold.topEffect();
    const float duck = fm10DryDuck != nullptr ? fm10DryDuck->load (std::memory_order_relaxed) * 0.01f : 0.0f;
    holdDry.setTargetValue (hold.engagedMask() != 0 && duck > 0.0f ? 1.0f - juce::jlimit (0.0f, 1.0f, duck) : 1.0f);
    holdDryActive = holdDry.isSmoothing() || holdDry.getTargetValue() < 1.0f;
    holdEngaged.store (hold.engagedMask(), std::memory_order_relaxed);
    holdTimedOut.store (hold.timedOutMask(), std::memory_order_relaxed);

    macroRuntime.chunkSeconds = (float) ((double) std::max (1, numSamples) / sampleRate);

    for (int t = 0; t < macros::kNumTargets; ++t)
        uiEffective[t].store (macroRuntime.effective01[t], std::memory_order_relaxed);

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
    {
        post (UiRequest::SceneApplied, (int) r.appliedId);
        emit (FillEventType::SceneApplied, outputPos);
    }

    // F-P02 Fill Now: resolve the quantised boundary into an absolute output sample for the engine.
    {
        auto fire = [&] (std::int64_t pos)
        {
            if (++fillNowId == 0)
                ++fillNowId;

            fillNowPos = pos;
            fillNowPending = false;
        };

        const auto mode = (FillNowQuant) fillNowQuantMode.load (std::memory_order_relaxed);

        if (fillNowRequested)
        {
            fillNowRequested = false;

            if (mode == FillNowQuant::Immediate || ! t.valid)
            {
                fire (outputPos);                       // no transport: immediate fallback
            }
            else
            {
                const auto guard = 0.001 * t.sampleRate / std::max (1.0, t.samplesPerBeat);
                fillNowBoundaryPpq = scenes::quantiser::nextBoundary (mode == FillNowQuant::NextBeat ? scenes::Quant::Beat : scenes::Quant::Bar,
                                                                      t.ppq, guard, t.tsNum, t.tsDen, 0, 0);
                fillNowPending = true;
                fillNowLastEndPpq = t.ppq;
            }
        }

        if (fillNowPending)
        {
            const auto spb = std::max (1.0, t.samplesPerBeat);
            const auto endPpq = t.ppq + (double) numSamples / spb;

            if (! t.valid)
            {
                fire (outputPos);
            }
            else if (t.ppq < fillNowLastEndPpq - 1.0e-6 || t.ppq > fillNowLastEndPpq + 1.0)
            {
                // loop / seek: re-arm from the new position, never drop
                const auto guard = 0.001 * t.sampleRate / spb;
                fillNowBoundaryPpq = scenes::quantiser::nextBoundary (mode == FillNowQuant::NextBeat ? scenes::Quant::Beat : scenes::Quant::Bar,
                                                                      t.ppq, guard, t.tsNum, t.tsDen, 0, 0);
            }
            else if (fillNowBoundaryPpq < endPpq)
            {
                fire (outputPos + (std::int64_t) std::llround (std::max (0.0, fillNowBoundaryPpq - t.ppq) * spb));
            }

            fillNowLastEndPpq = endPpq;
        }

        s.perf.fillNowId = fillNowId;
        s.perf.fillNowPos = fillNowPos;
        s.perf.fillNowOverSustained = fillNowOverSustainedOpt.load (std::memory_order_relaxed);
    }

    const auto& view = sceneEngine.view();
    s.perf.sceneView = view.active ? &view : nullptr;
    s.perf.panicked = panicCtl.isPanicked();

    // F-M18 Beat Battles: alternation by bar window, an overlay like a scene (no APVTS writes).
    {
        battle::BattleConfig fresh;

        if (battleModel.publisher().consume (fresh))
            battleRuntime.adopt (fresh);

        battleRuntime.everyBars = s.perf.battleEvery;
        battle::Status status = battle::Status::Off;

        if (battleRuntime.everyBars > 0)
        {
            if (! battleRuntime.haveStyles)
                status = battle::Status::NeedsStyles;
            else if (battleRuntime.styleAt (t.ppq, t.valid, t.tsNum, t.tsDen) < 0)
                status = battle::Status::NoTransport;
            else
                status = battleRuntime.styleAt (t.ppq, t.valid, t.tsNum, t.tsDen) == 0 ? battle::Status::StyleA : battle::Status::StyleB;
        }

        uiBattle.store ((int) status, std::memory_order_relaxed);
        s.perf.battle = (battleRuntime.everyBars > 0 && battleRuntime.haveStyles && ! panicCtl.isPanicked()) ? &battleRuntime : nullptr;
    }

    uiPpq.store (t.valid ? t.ppq : -1.0, std::memory_order_relaxed);
    uiTsNum.store (t.tsNum, std::memory_order_relaxed);
    uiTsDen.store (t.tsDen, std::memory_order_relaxed);
}

const float* LiveControl::dryDuckGains (int n) noexcept
{
    if (! holdDryActive)
        return nullptr;                                   // neutral: the processor skips the multiply entirely

    n = std::min (n, (int) holdDryGains.size());

    for (int i = 0; i < n; ++i)
        holdDryGains[(size_t) i] = holdDry.getNextValue();

    return holdDryGains.data();
}

void LiveControl::forceHoldsOff()
{
    for (const char* id : { "fm10_stutter", "fm10_freeze", "fm10_reverse", "fm10_tape" })
        if (auto* p = apvts.getParameter (id))
            if (p->getValue() > 0.0f)
                p->setValueNotifyingHost (0.0f);

    hold.panic();
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

    // ---- F-L24 LED feedback (message thread; never touches audio) ------------
    {
        commands::FeedbackState fs;
        fs.panicked = panicCtl.isPanicked();
        fs.currentScene = sceneCtl.getCurrentIndex();
        fs.pendingScene = sceneCtl.getPendingIndex();
        feedback.update (fs);

        if (++pumpTicks >= MidiLearnManager::learningTimerHz)      // about once per second
        {
            pumpTicks = 0;
            feedback.pollConnection (fs);
        }
    }

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
bool LiveControl::feedbackAvailableInThisHost() noexcept
{
    return juce::JUCEApplicationBase::isStandaloneApp();
}

bool LiveControl::applyProfile (const commands::Profile& profile, bool replace, juce::String& message)
{
    if (replace)
        bindingModel.clear();

    int skipped = 0;

    for (const auto& b : profile.bindings)
    {
        // A binding the user already owns, or one that collides with a CC mapped to a parameter, is skipped.
        const bool ccTaken = b.type == commands::MsgType::CC && midiLearn != nullptr
                             && midiLearn->getParameterIDForCC (b.number).isNotEmpty();

        if (ccTaken || bindingModel.add (b) != commands::BindingModel::AddResult::Ok)
            ++skipped;
    }

    bindingModel.setProfileName (profile.name.toStdString());
    feedback.setRules (profile.feedback);

    message = "Profile \"" + profile.name + "\" installed: " + juce::String ((int) profile.bindings.size() - skipped)
              + " binding(s)" + (skipped > 0 ? ", " + juce::String (skipped) + " skipped (already in use)" : juce::String())
              + (profile.feedback.empty() ? juce::String() : ", " + juce::String ((int) profile.feedback.size()) + " feedback rule(s)")
              + (feedbackAvailableInThisHost() ? "." : ". LED feedback works in the Standalone application only.");
    return true;
}

void LiveControl::setFeedbackSink (std::unique_ptr<commands::MidiSink> sink)
{
    feedback.setSink (std::move (sink));
}

void LiveControl::setFeedbackDevice (const juce::String& deviceName)
{
    bindingModel.setFeedbackDevice (deviceName.toStdString());

    if (deviceName.isEmpty() || ! feedbackAvailableInThisHost())
    {
        feedback.setSink (nullptr);
        feedbackStatus = deviceName.isEmpty() ? juce::String() : "LED feedback works in the Standalone application only.";
        return;
    }

    juce::String error;
    feedback.setSink (commands::makeDeviceSink (deviceName, error));
    feedbackStatus = error;
}

void LiveControl::refreshFeedbackAfterRestore()
{
    feedback.setRules ({});
    const auto wanted = juce::String (bindingModel.getProfileName());

    if (wanted.isNotEmpty())
    {
        for (const auto& p : commands::scanLibrary (commands::defaultUserDirectory()).profiles)
            if (p.name == wanted)
                feedback.setRules (p.profile.feedback);
    }

    setFeedbackDevice (juce::String (bindingModel.getFeedbackDevice()));
}

//------------------------------------------------------------------------------
PerfOptions LiveControl::getPerfOptions() const noexcept
{
    PerfOptions o;
    o.panic = panicCtl.getOptions();
    o.fillNowQuant = (FillNowQuant) fillNowQuantMode.load (std::memory_order_relaxed);
    o.fillNowOverSustained = fillNowOverSustainedOpt.load (std::memory_order_relaxed);
    return o;
}

void LiveControl::setPerfOptions (const PerfOptions& o) noexcept
{
    panicCtl.setOptions (o.panic);
    fillNowQuantMode.store ((std::uint8_t) o.fillNowQuant, std::memory_order_relaxed);
    fillNowOverSustainedOpt.store (o.fillNowOverSustained, std::memory_order_relaxed);
}

juce::ValueTree LiveControl::perfToValueTree() const
{
    const auto o = getPerfOptions();
    juce::ValueTree t ("Perf");
    t.setProperty ("v", 1, nullptr);
    t.setProperty ("recovery", (int) o.panic.recovery, nullptr);
    t.setProperty ("panicToggles", o.panic.panicToggles ? 1 : 0, nullptr);
    t.setProperty ("fillNowQuant", (int) o.fillNowQuant, nullptr);
    t.setProperty ("fillNowSustained", o.fillNowOverSustained ? 1 : 0, nullptr);
    return t;
}

bool LiveControl::perfValidate (const juce::ValueTree& t)
{
    if (! t.hasType ("Perf"))
        return false;

    const auto rec = (int) t.getProperty ("recovery", 0);
    const auto q = (int) t.getProperty ("fillNowQuant", 1);
    return rec >= 0 && rec <= 3 && q >= 0 && q <= 2;
}

void LiveControl::perfRestore (const juce::ValueTree& t)
{
    PerfOptions o;

    if (t.hasType ("Perf"))
    {
        o.panic.recovery = (panic::RecoveryMode) juce::jlimit (0, 3, (int) t.getProperty ("recovery", 0));
        o.panic.panicToggles = (int) t.getProperty ("panicToggles", 1) != 0;
        o.fillNowQuant = (FillNowQuant) juce::jlimit (0, 2, (int) t.getProperty ("fillNowQuant", 1));
        o.fillNowOverSustained = (int) t.getProperty ("fillNowSustained", 0) != 0;
    }

    setPerfOptions (o);
}

} // namespace gapfill::live
