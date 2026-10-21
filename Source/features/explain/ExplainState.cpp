#include "ExplainState.h"

#include "../../PluginProcessor.h"
#include "../events/Diagnostics.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace gapfill::explain
{

float peakOf (const float* samples, int n) noexcept
{
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
        peak = juce::jmax (peak, std::abs (samples[i]));

    return peak;
}

ExplainState::ExplainState() = default;

void ExplainState::attach (GapFillAudioProcessor& proc, juce::AudioProcessorValueTreeState& state)
{
    owner = &proc;
    apvts = &state;

    std::vector<std::atomic<float>*> raw;

    for (auto* p : proc.getParameters())
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p);
        raw.push_back (withId != nullptr ? state.getRawParameterValue (withId->paramID) : nullptr);
    }

    recorder.attachParameters (std::move (raw));

    fixit::Controller::Hooks hooks;
    hooks.pushUndoSnapshot = [this] { if (owner != nullptr) owner->pushUndoSnapshot(); };
    hooks.runAction = [this] (fixit::ActionKind a)
    {
        if (a == fixit::ActionKind::Recover && owner != nullptr)
        {
            Command c;
            c.type = CommandType::Recover;
            owner->postCommand (c);
        }
        else if (a == fixit::ActionKind::OpenTempoOptions && openOptions != nullptr)
        {
            openOptions();
        }
    };
    fixitController.setHooks (std::move (hooks));
}

//==============================================================================
void ExplainState::onChunk (const ChunkInfo& c) noexcept
{
    sampleRate.store (c.sampleRate, std::memory_order_relaxed);

    // Transport jump: same rule as the processor's (more than a quarter beat off the expected position).
    bool jumped = false;

    if (c.blockStart)
    {
        if (c.transportValid && lastTransportValid)
        {
            const auto samplesPerBeat = 60.0 / juce::jmax (1.0, c.bpm) * c.sampleRate;
            const auto expected = lastPpq + (double) lastChunkSamples / samplesPerBeat;
            jumped = std::abs (c.ppq - expected) > 0.25;
        }

        lastTransportValid = c.transportValid;
        lastPpq = c.ppq;
        lastChunkSamples = c.numSamples;
    }

    health::Observations o;
    o.numSamples = c.numSamples;
    o.sampleRate = c.sampleRate;
    o.transportValid = c.transportValid;
    o.tempoFeatureOn = c.tempoFeatureOn;
    o.transportJumped = jumped;
    o.scRequested = c.scRequested;
    o.scBusEnabled = c.scBusEnabled;
    o.scPeak = c.scPeak;
    o.cpu01 = cpu.isEnabled() ? cpu.getCpu01() : 0.0f;

    const auto overruns = cpu.getOverrunCount();
    o.deadlineOverrun = overruns != lastOverruns;
    lastOverruns = overruns;

    o.droppedEvents = c.droppedTotal > lastDropped ? c.droppedTotal - lastDropped : 0;
    lastDropped = c.droppedTotal;
    o.bypassed = c.bypassed;
    o.panicActive = c.panicActive;
    o.freeTimingFallback = c.freeTimingFallback;

    healthMonitor.update (o);

    events::EventRecorder::Chunk rc;
    rc.numSamples = c.numSamples;
    rc.blockStart = c.blockStart;
    rc.transportValid = c.transportValid;
    rc.ppq = c.ppq;
    rc.bpm = c.bpm;
    rc.tsNum = c.tsNum;
    rc.tsDen = c.tsDen;
    recorder.onChunk (rc);
}

//==============================================================================
void ExplainState::onFillEvent (const FillEvent& e) noexcept
{
    timelineModel.pushEngineEvent (e);
    newestEventPos = juce::jmax (newestEventPos, e.pos);
}

void ExplainState::noteQueueDrops (std::uint32_t droppedOutcomes, std::uint32_t droppedOptional) noexcept
{
    if (droppedOutcomes != lastDroppedOutcomes || droppedOptional != lastDroppedOptional)
        timelineModel.noteDropped (newestEventPos);

    lastDroppedOutcomes = droppedOutcomes;
    lastDroppedOptional = droppedOptional;
}

void ExplainState::timerTick (std::uint32_t nowMs)
{
    if (owner == nullptr || apvts == nullptr)
        return;

    if (recordToggleRequested.exchange (false))
        events::toggleRecording (*owner);

    // F-D03: Quality = Auto needs the CPU estimate even with the editor closed.
    const auto* qualityMode = apvts->getRawParameterValue ("qualityMode");
    const bool qualityAuto = qualityMode != nullptr && qualityMode->load() > 2.5f;
    cpu.setEnabled (owner->getVisualisationFifo().isEnabled() || qualityAuto);
    healthTracker.update (healthMonitor.getMask(), nowMs);

    if (recorder.isRecording() || recorder.isFull())
        recorder.drain();

    // Fix-it runs at about 2 Hz and never on the audio thread.
    if (++fixitDivider < 7)
        return;

    fixitDivider = 0;

    const auto& log = owner->getGapOutcomeLog();
    FillEvent newestFirst[fixit::Evidence::windowGaps];
    const auto n = juce::jmin (log.size(), (int) fixit::Evidence::windowGaps);

    for (int i = 0; i < n; ++i)
        newestFirst[i] = log.get (i);

    const auto mask = healthMonitor.getMask();
    const auto evidence = fixit::buildEvidence (newestFirst, n, mask,
                                                healthTracker.secondsActive (health::HealthCode::OverloadWarn, nowMs),
                                                healthTracker.secondsActive (health::HealthCode::PanicActive, nowMs));

    fixit::ApvtsParamAccess access (*apvts);
    fixitController.tick (nowMs, evidence, access, showSuggestions.load(), mutedFixit.load());
}

bool ExplainState::applyFixit (bool confirmed)
{
    if (apvts == nullptr)
        return false;

    fixit::ApvtsParamAccess access (*apvts);
    return fixitController.apply (juce::Time::getMillisecondCounter(), access, confirmed);
}

bool ExplainState::undoFixit()
{
    if (apvts == nullptr)
        return false;

    fixit::ApvtsParamAccess access (*apvts);
    return fixitController.undo (access);
}

void ExplainState::dismissFixit()
{
    fixitController.dismiss (juce::Time::getMillisecondCounter());
}

void ExplainState::muteFixitRule (fixit::RuleId r)
{
    mutedFixit.fetch_or (1u << (unsigned) r);
    fixitController.dismiss (juce::Time::getMillisecondCounter());
}

void ExplainState::muteHealthCode (health::HealthCode c, bool shouldMute) noexcept
{
    if (! health::isMutable (c))
        return;

    if (shouldMute)
        mutedHealth.fetch_or (health::bitOf (c));
    else
        mutedHealth.fetch_and (~health::bitOf (c));
}

//==============================================================================
// <Health> subtree
namespace
{
    constexpr const char* kType = "Health";

    bool intInRange (const juce::ValueTree& t, const char* name, int lo, int hi)
    {
        if (! t.hasProperty (name))
            return true;                             // optional attribute => default

        // After an XML round trip numbers arrive as strings: accept digits only.
        const auto text = t.getProperty (name).toString().trim();

        if (text.isEmpty() || text.length() > 10 || ! text.containsOnly ("-0123456789"))
            return false;

        const auto n = text.getLargeIntValue();
        return n >= lo && n <= hi;
    }
}

bool validateSubtree (const juce::ValueTree& t)
{
    if (! t.hasType (kType) || t.getNumChildren() != 0 || t.getNumProperties() > 8)
        return false;

    return intInRange (t, "muted", 0, (int) health::allCodesMask)
        && intInRange (t, "fixitMuted", 0, (1 << fixit::numRules) - 1)
        && intInRange (t, "suggestions", 0, 1)
        && intInRange (t, "legendCollapsed", 0, 1)
        && intInRange (t, "zoom", 4, 16)
        && (! t.hasProperty ("zoom") || (int) t.getProperty ("zoom") == 4 || (int) t.getProperty ("zoom") == 8
                                      || (int) t.getProperty ("zoom") == 16);
}

void applySubtree (GapFillAudioProcessor& p, const juce::ValueTree& t)
{
    auto& s = p.getExplainState();
    s.mutedHealth.store (((std::uint32_t) (int) t.getProperty ("muted", 0)) & ~health::bitOf (health::HealthCode::PanicActive)
                                                                           & ~health::bitOf (health::HealthCode::OverloadCritical));
    s.mutedFixit.store ((std::uint32_t) (int) t.getProperty ("fixitMuted", 0));
    s.showSuggestions.store ((int) t.getProperty ("suggestions", 1) != 0);
    s.legendCollapsed.store ((int) t.getProperty ("legendCollapsed", 0) != 0);
    s.timelineZoomSeconds.store ((int) t.getProperty ("zoom", 8));
}

juce::ValueTree serialiseSubtree (const GapFillAudioProcessor& p)
{
    const auto& s = p.getExplainState();
    const auto muted = s.mutedHealth.load();
    const auto fixitMuted = s.mutedFixit.load();
    const bool suggestions = s.showSuggestions.load();
    const bool collapsed = s.legendCollapsed.load();
    const auto zoom = s.timelineZoomSeconds.load();

    if (muted == 0 && fixitMuted == 0 && suggestions && ! collapsed && zoom == 8)
        return {};                                   // all defaults: write nothing

    juce::ValueTree t (kType);
    t.setProperty ("muted", (int) muted, nullptr);
    t.setProperty ("fixitMuted", (int) fixitMuted, nullptr);
    t.setProperty ("suggestions", suggestions ? 1 : 0, nullptr);
    t.setProperty ("legendCollapsed", collapsed ? 1 : 0, nullptr);
    t.setProperty ("zoom", zoom, nullptr);
    return t;
}

void resetSubtree (GapFillAudioProcessor& p)
{
    auto& s = p.getExplainState();
    s.mutedHealth.store (0);
    s.mutedFixit.store (0);
    s.showSuggestions.store (true);
    s.legendCollapsed.store (false);
    s.timelineZoomSeconds.store (8);
}

} // namespace gapfill::explain
