#include "MidiLearn.h"
#include "Parameters.h"
#include "features/controller/Takeover.h"

namespace gapfill
{

//==============================================================================
namespace { thread_local const MidiLearnManager* applyingMidi = nullptr; }
struct MidiLearnManager::Extended
{
    struct Listener final : juce::AudioProcessorParameter::Listener
    {
        Listener (MidiLearnManager& m, juce::RangedAudioParameter& p) : manager (m), parameter (p) { parameter.addListener (this); }
        ~Listener() override { parameter.removeListener (this); }
        void parameterValueChanged (int, float) override
        { if (applyingMidi != &manager) changed.store (true, std::memory_order_relaxed); }
        void parameterGestureChanged (int, bool) override {}
        MidiLearnManager& manager;
        juce::RangedAudioParameter& parameter;
        std::atomic<bool> changed { false };
    };
    std::vector<std::unique_ptr<Listener>> listeners;
    std::array<std::atomic<std::uint32_t>, 128> options {}, pending {}, received {};
    std::array<controller::Takeover, 128> pickup;
    std::atomic<std::uint64_t> lastInput { 0 };
    std::uint32_t sequence = 0; // sole audio writer
    std::function<juce::String (int, int, bool)> conflict;
    std::function<void()> learnStart;
    juce::String status;
};
MidiLearnManager::~MidiLearnManager() = default;
MidiLearnManager::MidiLearnManager() : extended (std::make_unique<Extended>())
{
    for (int cc = 0; cc < numCCs; ++cc)
    {
        ccToParam[(size_t) cc].store (-1, std::memory_order_relaxed);
        pendingParamIndex[(size_t) cc].store (-1, std::memory_order_relaxed);
        pendingValue[(size_t) cc].store (0.0f, std::memory_order_relaxed);
        pendingDirty[(size_t) cc].store (false, std::memory_order_relaxed);
    }
}

void MidiLearnManager::connect (juce::AudioProcessorValueTreeState& state)
{
    apvts = &state;
    parameterIDs = allParameterIDs();

    extended->listeners.clear();
    parameters.clear();
    parameters.reserve ((size_t) parameterIDs.size());

    for (const auto& id : parameterIDs)
    {
        auto* parameter = state.getParameter (id);
        parameters.push_back (parameter);
        extended->listeners.push_back (parameter != nullptr ? std::make_unique<Extended::Listener> (*this, *parameter) : nullptr);
    }
}

int MidiLearnManager::indexOf (const juce::String& parameterID) const
{
    return parameterIDs.indexOf (parameterID);
}

//==============================================================================
void MidiLearnManager::processMidi (const juce::MidiBuffer& midi) noexcept
{
    if (midi.isEmpty())
        return;

    for (const auto meta : midi)
    {
        // Read short-message bytes directly: constructing a MidiMessage for a
        // long SysEx packet could allocate on the callback thread.
        if (meta.numBytes < 1) continue;
        const auto status = meta.data[0];
        const auto kind = status & 0xf0;
        const bool isCC = kind == 0xb0 && meta.numBytes == 3 && meta.data[1] < 128 && meta.data[2] < 128;
        const bool isNote = (kind == 0x80 || kind == 0x90) && meta.numBytes == 3 && meta.data[1] < 128 && meta.data[2] < 128;
        const bool isProgram = kind == 0xc0 && meta.numBytes == 2 && meta.data[1] < 128;
        if (isCC || isNote || isProgram)
        {
            const auto packed = ((std::uint64_t) ++extended->sequence << 32)
                              | ((std::uint64_t) status << 16) | ((std::uint64_t) meta.data[1] << 8)
                              | (isProgram ? 0 : meta.data[2]);
            extended->lastInput.store (packed, std::memory_order_relaxed);
        }
        if (! isCC) continue;
        const int cc = meta.data[1];
        const int channel = (status & 0x0f) + 1;
        extended->received[(std::size_t) cc].store ((std::uint32_t) channel, std::memory_order_release);
        int learning = learnTarget.load (std::memory_order_relaxed);
        if (learning >= 0)
        {
            const auto claimed = learning;
            if (learnTarget.compare_exchange_strong (learning, -1))
            {
                learnTicksRemaining.store (0, std::memory_order_relaxed);
                int start1, size1, start2, size2;
                fifo.prepareToWrite (1, start1, size1, start2, size2);
                if (size1 > 0) events[(std::size_t) start1] = { -1, claimed, cc, channel, 0.0f };
                fifo.finishedWrite (size1 + size2);
            }
            continue;
        }
        const auto target = ccToParam[(std::size_t) cc].load (std::memory_order_relaxed);
        const auto options = extended->options[(std::size_t) cc].load (std::memory_order_relaxed);
        const auto filter = options & 31u;
        if (target < 0 || target >= 65535 || (filter != 0 && filter != (std::uint32_t) channel)) continue;
        // One atomic word keeps the latest target/value/channel coherent during
        // remapping and dense input. The consumer rejects obsolete targets.
        const auto pending = (std::uint32_t) (target + 1) | ((std::uint32_t) meta.data[2] << 16)
                           | ((std::uint32_t) channel << 23);
        extended->pending[(std::size_t) cc].store (pending, std::memory_order_release);
    }
}

//==============================================================================
bool MidiLearnManager::applyPendingChanges()
{
    bool learnCompleted = false;

    int start1, size1, start2, size2;
    fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);

    const auto handle = [this, &learnCompleted] (const Event& e)
    {
        if (e.paramIndex < 0)
        {
            const auto index = e.learnIndex;

            if (! juce::isPositiveAndBelow (index, parameterIDs.size()))
                return;

            if (! configureMapping (parameterIDs[index], e.cc, {})) return;
            learnCompleted = true;
            return;
        }

        if (! juce::isPositiveAndBelow (e.paramIndex, (int) parameters.size()))
            return;

        if (auto* p = parameters[(size_t) e.paramIndex])
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, e.value));
    };

    for (int i = 0; i < size1; ++i)
        handle (events[(size_t) (start1 + i)]);

    for (int i = 0; i < size2; ++i)
        handle (events[(size_t) (start2 + i)]);

    fifo.finishedRead (size1 + size2);

    // Coalesced receipt is a single atomic packet, independent of feedback's
    // receipt lane. Pickup observes the current normalized APVTS value here.
    for (int cc = 0; cc < numCCs; ++cc)
    {
        const auto pending = extended->pending[(std::size_t) cc].exchange (0, std::memory_order_acq_rel);
        if (pending == 0) continue;
        const int target = (int) (pending & 65535u) - 1;
        if (! juce::isPositiveAndBelow (target, (int) parameters.size())
            || ccToParam[(std::size_t) cc].load (std::memory_order_relaxed) != target) continue;
        const auto options = extended->options[(std::size_t) cc].load (std::memory_order_relaxed);
        const auto channel = (pending >> 23) & 31u;
        if ((options & 31u) != 0 && (options & 31u) != channel) continue;
        if (auto* parameter = parameters[(std::size_t) target])
        {
            const float value = (float) ((pending >> 16) & 127u) / 127.0f;
            const auto takeover = (options & 32u) != 0 ? controller::TakeoverMode::Pickup : controller::TakeoverMode::Jump;
            auto& pickup = extended->pickup[(std::size_t) cc];
            if (auto* listener = extended->listeners[(std::size_t) target].get(); listener != nullptr
                && listener->changed.exchange (false, std::memory_order_relaxed)) pickup.reset();
            if (pickup.accepts (takeover, value, parameter->getValue()))
            {
                const juce::ScopedValueSetter<const MidiLearnManager*> applying (applyingMidi, this);
                parameter->setValueNotifyingHost (value);
                pickup.applied (parameter->getValue());
            }
        }
    }

    return learnCompleted;
}

//==============================================================================
void MidiLearnManager::startLearning (const juce::String& parameterID)
{
    const auto index = indexOf (parameterID);

    if (index >= 0)
    {
        if (extended->learnStart) extended->learnStart();
        extended->status = "Waiting for MIDI";
        learnTicksRemaining.store (learningTimeoutTicks, std::memory_order_relaxed);
        learnTarget.store (index, std::memory_order_release);
    }
}

void MidiLearnManager::stopLearning()
{
    learnTarget.store (-1, std::memory_order_release);
    learnTicksRemaining.store (0, std::memory_order_relaxed);
}

bool MidiLearnManager::advanceLearningTimeout() noexcept
{
    if (learnTarget.load (std::memory_order_acquire) < 0)
        return false;

    const auto remaining = learnTicksRemaining.load (std::memory_order_relaxed);

    if (remaining > 1)
    {
        learnTicksRemaining.store (remaining - 1, std::memory_order_relaxed);
        return false;
    }

    stopLearning();
    return true;
}

juce::String MidiLearnManager::getLearningParameterID() const
{
    const auto index = learnTarget.load (std::memory_order_relaxed);

    return juce::isPositiveAndBelow (index, parameterIDs.size()) ? parameterIDs[index] : juce::String();
}

juce::String MidiLearnManager::getParameterIDForCC (int cc) const
{
    if (cc < 0 || cc >= numCCs)
        return {};

    const auto index = ccToParam[(size_t) cc].load (std::memory_order_relaxed);
    return index >= 0 && index < parameterIDs.size() ? parameterIDs[index] : juce::String();
}

int MidiLearnManager::getMappedCC (const juce::String& parameterID) const
{
    const auto index = indexOf (parameterID);

    if (index < 0)
        return -1;

    for (int cc = 0; cc < numCCs; ++cc)
        if (ccToParam[(size_t) cc].load (std::memory_order_relaxed) == index)
            return cc;

    return -1;
}

void MidiLearnManager::setMapping (const juce::String& parameterID, int cc)
{
    const auto index = indexOf (parameterID);

    if (index < 0 || ! juce::isPositiveAndBelow (cc, numCCs))
        return;

    for (int i = 0; i < numCCs; ++i)
        if (ccToParam[(size_t) i].load (std::memory_order_relaxed) == index)
        {
            ccToParam[(size_t) i].store (-1, std::memory_order_relaxed);
            pendingDirty[(size_t) i].store (false, std::memory_order_relaxed);
            extended->pending[(std::size_t) i].store (0, std::memory_order_relaxed);
        }

    pendingDirty[(size_t) cc].store (false, std::memory_order_relaxed);
    extended->pending[(std::size_t) cc].store (0, std::memory_order_relaxed);
    extended->options[(std::size_t) cc].store (0, std::memory_order_relaxed);
    extended->pickup[(std::size_t) cc].reset();
    ccToParam[(size_t) cc].store (index, std::memory_order_relaxed);
}

void MidiLearnManager::clearMapping (const juce::String& parameterID)
{
    const auto index = indexOf (parameterID);

    if (index < 0)
        return;

    for (int cc = 0; cc < numCCs; ++cc)
        if (ccToParam[(size_t) cc].load (std::memory_order_relaxed) == index)
        {
            ccToParam[(size_t) cc].store (-1, std::memory_order_relaxed);
            pendingDirty[(size_t) cc].store (false, std::memory_order_relaxed);
        }
}

void MidiLearnManager::clearAllMappings()
{
    stopLearning();

    for (int cc = 0; cc < numCCs; ++cc)
    {
        ccToParam[(size_t) cc].store (-1, std::memory_order_relaxed);
        pendingDirty[(size_t) cc].store (false, std::memory_order_relaxed);
        pendingParamIndex[(size_t) cc].store (-1, std::memory_order_relaxed);
        extended->options[(std::size_t) cc].store (0, std::memory_order_relaxed);
        extended->pending[(std::size_t) cc].store (0, std::memory_order_relaxed);
        extended->pickup[(std::size_t) cc].reset();
    }
}

int MidiLearnManager::getNumMappings() const
{
    int count = 0;

    for (const auto& slot : ccToParam)
        if (slot.load (std::memory_order_relaxed) >= 0)
            ++count;

    return count;
}

//==============================================================================
juce::ValueTree MidiLearnManager::toValueTree() const
{
    juce::ValueTree tree (stateType);

    for (int cc = 0; cc < numCCs; ++cc)
    {
        const auto index = ccToParam[(size_t) cc].load (std::memory_order_relaxed);

        if (! juce::isPositiveAndBelow (index, parameterIDs.size()))
            continue;

        juce::ValueTree entry ("MAP");
        entry.setProperty ("param", parameterIDs[index], nullptr);
        entry.setProperty ("cc", cc, nullptr);
        tree.appendChild (entry, nullptr);
    }

    return tree;
}

void MidiLearnManager::restoreFromValueTree (const juce::ValueTree& tree)
{
    clearAllMappings();

    if (! tree.isValid() || ! tree.hasType (stateType))
        return;

    for (const auto& entry : tree)
    {
        // Unknown ids are ignored rather than rejected, so a project saved by a
        // later version still loads what it can.
        setMapping (entry.getProperty ("param").toString(), (int) entry.getProperty ("cc", -1));
    }
}

} // namespace gapfill

namespace gapfill
{
bool MidiLearnManager::configureMapping (const juce::String& id, int cc, controller::MappingOptions options, bool replace)
{
    const auto index = indexOf (id);
    if (index < 0 || ! juce::isPositiveAndBelow (cc, numCCs) || options.channel > 16
        || (int) options.takeover > 1 || parameters[(std::size_t) index] == nullptr)
    { extended->status = "Invalid parameter mapping"; return false; }
    const auto owner = getParameterIDForCC (cc);
    if (! replace && owner.isNotEmpty() && owner != id)
    { extended->status = "CC " + juce::String (cc) + " already controls " + owner; return false; }
    if (extended->conflict)
    {
        const auto conflict = extended->conflict (cc, options.channel, replace);
        if (conflict.isNotEmpty()) { extended->status = conflict; return false; }
    }
    setMapping (id, cc);
    setMappingOptions (id, options);
    extended->status = "CC " + juce::String (cc) + " mapped to " + id;
    return true;
}
controller::MappingOptions MidiLearnManager::getMappingOptions (const juce::String& id) const
{
    const int cc = getMappedCC (id);
    if (cc < 0) return {};
    const auto packed = extended->options[(std::size_t) cc].load (std::memory_order_relaxed);
    return { (std::uint8_t) (packed & 31u), (packed & 32u) != 0 ? controller::TakeoverMode::Pickup : controller::TakeoverMode::Jump };
}
void MidiLearnManager::setMappingOptions (const juce::String& id, controller::MappingOptions options)
{
    const int cc = getMappedCC (id);
    if (cc < 0 || options.channel > 16 || (int) options.takeover > 1) return;
    extended->options[(std::size_t) cc].store ((std::uint32_t) options.channel
        | (options.takeover == controller::TakeoverMode::Pickup ? 32u : 0u), std::memory_order_relaxed);
    extended->pending[(std::size_t) cc].store (0, std::memory_order_relaxed);
    extended->pickup[(std::size_t) cc].reset();
}
void MidiLearnManager::resetMappingOptions()
{
    for (std::size_t i = 0; i < extended->options.size(); ++i)
    { extended->options[i].store (0, std::memory_order_relaxed); extended->pending[i].store (0, std::memory_order_relaxed); extended->pickup[i].reset(); }
}
std::vector<controller::ParameterBinding> MidiLearnManager::parameterBindings() const
{
    std::vector<controller::ParameterBinding> result;
    for (int cc = 0; cc < numCCs; ++cc)
    {
        const auto id = getParameterIDForCC (cc);
        if (id.isEmpty()) continue;
        const auto options = getMappingOptions (id);
        result.push_back ({ id.toStdString(), (std::uint8_t) cc, options.channel, options.takeover });
    }
    return result;
}
const juce::String& MidiLearnManager::getMappingStatus() const noexcept { return extended->status; }
void MidiLearnManager::setCommandConflictHandler (std::function<juce::String(int,int,bool)> fn) { extended->conflict=std::move(fn); }
void MidiLearnManager::setLearnStartHook (std::function<void()> fn) { extended->learnStart=std::move(fn); }
MidiLearnManager::InputMessage MidiLearnManager::lastInput() const noexcept
{
    const auto packed = extended->lastInput.load (std::memory_order_relaxed);
    return { (std::uint8_t) (packed >> 16), (std::uint8_t) (packed >> 8), (std::uint8_t) packed, (std::uint32_t) (packed >> 32) };
}
bool MidiLearnManager::takeReceivedCC (int cc, std::uint8_t& channel) noexcept
{
    if (! juce::isPositiveAndBelow (cc, numCCs)) return false;
    const auto received = extended->received[(std::size_t) cc].exchange (0, std::memory_order_acq_rel);
    if (received == 0) return false;
    channel = (std::uint8_t) received;
    return true;
}
}
