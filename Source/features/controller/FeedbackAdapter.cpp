#include "FeedbackAdapter.h"
#include "../../MidiLearn.h"
#include <cmath>

#ifndef GAPFILL_MIDI_OUT
#define GAPFILL_MIDI_OUT 0
#endif
namespace gapfill::controller
{
FeedbackAdapter::FeedbackAdapter (commands::FeedbackEngine& engine) : owner (engine) {}
FeedbackAdapter::~FeedbackAdapter() { stopTimer(); }
bool FeedbackAdapter::configure (const commands::Profile& profile, juce::AudioProcessorValueTreeState& state,
                                 MidiLearnManager* learn, std::function<commands::FeedbackState()> stateSource)
{
    if (! profile.extended || profile.params.size() > kMaxParameterBindings || profile.feedback.size() > kMaxIndicators) return false;
    std::array<OutEvent, kMaxFeedbackOutputs> addresses {};
    std::array<juce::RangedAudioParameter*, kMaxParameterBindings> found {};
    std::array<int, kMaxIndicators> indicatorIndexes {};
    std::size_t count = 0, numParameters = 0;
    if (profile.parameterFeedback)
        for (const auto& binding : profile.params)
        {
            auto* parameter = state.getParameter (juce::String (binding.id));
            if (parameter == nullptr || binding.cc > 127 || profile.txChannel < 1 || profile.txChannel > 16) return false;
            found[numParameters++] = parameter;
            addresses[count++] = { (std::uint8_t) (0xb0 | (profile.txChannel - 1)), binding.cc, 0 };
        }
    std::size_t index = 0;
    for (const auto& rule : profile.feedback)
    {
        const OutEvent address { (std::uint8_t) ((rule.type == commands::MsgType::CC ? 0xb0 : 0x90) | (rule.channel - 1)), rule.number, rule.offValue };
        if ((rule.type != commands::MsgType::CC && rule.type != commands::MsgType::Note) || rule.channel < 1 || rule.channel > 16) return false;
        std::size_t outputIndex = count;
        for (std::size_t i = numParameters; i < count; ++i)
            if (addresses[i].status == address.status && addresses[i].data1 == address.data1) { outputIndex = i; break; }
        if (outputIndex == count) addresses[count++] = address;
        indicatorIndexes[index++] = (int) outputIndex;
    }
    FeedbackCore candidate;
    if (! candidate.configure ({ addresses.data(), count })) return false;
    stopTimer();
    core = candidate;
    configuration = profile;
    parameters = found; parameterCount = numParameters; indicatorOutput = indicatorIndexes;
    midiLearn = learn; source = std::move (stateSource); configured = true; nextDevicePoll = -1.0;
    generation.fetch_add (1, std::memory_order_release);
    updateTimer();
    return true;
}
void FeedbackAdapter::disable()
{
    stopTimer(); configured = false; configuration = {}; source = {}; midiLearn = nullptr;
    generation.fetch_add (1, std::memory_order_release);
}
void FeedbackAdapter::sinkChanged() { resync(); nextDevicePoll = -1.0; updateTimer(); }
void FeedbackAdapter::enableHostOutput (bool enabled)
{
    hostOutput = enabled && GAPFILL_MIDI_OUT != 0;
    generation.fetch_add (1, std::memory_order_release);
    resync(); updateTimer();
}
void FeedbackAdapter::updateTimer()
{
    if (configured && core.size() != 0 && (owner.sink != nullptr || hostOutput)) startTimerHz (30);
    else stopTimer();
}
void FeedbackAdapter::resync() { core.resync (juce::Time::getMillisecondCounterHiRes()); }
void FeedbackAdapter::timerCallback() { tick (juce::Time::getMillisecondCounterHiRes()); }
bool FeedbackAdapter::send (void* context, OutEvent event) noexcept
{
    auto& adapter = *static_cast<FeedbackAdapter*> (context);
    if (adapter.owner.sink != nullptr)
    {
        const juce::MidiMessage message (event.status, event.data1, event.data2);
        if (! adapter.owner.sink->send (message))
        { adapter.owner.connected = false; adapter.deviceLost.store (true, std::memory_order_release); return false; }
    }
    else if (adapter.hostOutput)
    {
        if (! adapter.output.push ({ event, adapter.generation.load (std::memory_order_relaxed) })) return false;
    }
    else return false;
    ++adapter.owner.totalSent;
    return true;
}
void FeedbackAdapter::tick (double now)
{
    if (! configured || ! std::isfinite (now)) return;
    if (midiLearn != nullptr)
        for (int cc = 0; cc < 128; ++cc)
        {
            std::uint8_t channel = 0;
            if (midiLearn->takeReceivedCC (cc, channel)) core.noteInput (channel, (std::uint8_t) cc, now);
        }
    for (std::size_t i = 0; i < parameterCount; ++i)
        core.setValue (i, (std::uint8_t) juce::jlimit (0, 127, (int) std::lround (parameters[i]->getValue() * 127.0f)));
    const auto state = source ? source() : commands::FeedbackState {};
    // Repeated LED addresses use the first resting value and last active rule,
    // matching the legacy engine's current/pending-scene priority.
    std::array<int, kMaxFeedbackOutputs> values; values.fill (-1);
    for (std::size_t i = 0; i < configuration.feedback.size(); ++i)
    {
        const auto outputIndex = (std::size_t) indicatorOutput[i];
        const auto& rule = configuration.feedback[i];
        if (values[outputIndex] < 0) values[outputIndex] = rule.offValue;
        if (commands::FeedbackEngine::isOn (rule, state)) values[outputIndex] = rule.onValue;
    }
    for (std::size_t i = parameterCount; i < core.size(); ++i) core.setValue (i, (std::uint8_t) values[i]);
    if (owner.sink != nullptr && (nextDevicePoll < 0.0 || now >= nextDevicePoll))
    {
        nextDevicePoll = now + 100.0;
        const bool wasConnected = owner.connected;
        const bool present = owner.sink->isConnected();
        if (wasConnected && ! present) deviceLost.store (true, std::memory_order_release);
        owner.connected = present || owner.sink->tryReconnect();
        if ((! wasConnected || ! present) && owner.connected) core.resync (now);
    }
    if (owner.sink == nullptr || owner.connected) core.tick (now, send, this);
}
int FeedbackAdapter::pop (OutEvent* destination, int maximum) noexcept
{
    if (destination == nullptr || maximum <= 0) return 0;
    int written = 0;
    const auto activeGeneration = generation.load (std::memory_order_acquire);
    // A disabled/replaced profile can leave stale packets. Drain at most one
    // queue capacity, accepting only the current generation; never reset a live FIFO.
    for (int i = 0; i < kOutputQueueCapacity && written < maximum; ++i)
    {
        Queued event;
        if (output.pop (&event, 1) != 1) break;
        if (event.generation == activeGeneration) destination[written++] = event.event;
    }
    return written;
}
}
