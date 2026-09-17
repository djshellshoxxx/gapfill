#include "MidiLearn.h"
#include "Parameters.h"

namespace gapfill
{

//==============================================================================
MidiLearnManager::MidiLearnManager()
{
    for (auto& slot : ccToParam)
        slot.store (-1, std::memory_order_relaxed);
}

void MidiLearnManager::connect (juce::AudioProcessorValueTreeState& state)
{
    apvts = &state;
    parameterIDs = allParameterIDs();

    parameters.clear();
    parameters.reserve ((size_t) parameterIDs.size());

    for (const auto& id : parameterIDs)
        parameters.push_back (state.getParameter (id));
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
        const auto message = meta.getMessage();

        if (! message.isController())
            continue;

        const auto cc = message.getControllerNumber();

        if (! juce::isPositiveAndBelow (cc, numCCs))
            continue;

        const auto value = (float) message.getControllerValue() / 127.0f;

        // A pending learn claims the first CC that moves.
        int learning = learnTarget.load (std::memory_order_relaxed);

        if (learning >= 0)
        {
            const auto claimed = learning;

            // Hand the binding back to the message thread, which owns the table.
            // The CAS makes sure only one CC can win the learn.
            if (learnTarget.compare_exchange_strong (learning, -1))
            {
                int start1, size1, start2, size2;
                fifo.prepareToWrite (1, start1, size1, start2, size2);

                if (size1 > 0)
                    events[(size_t) start1] = { -1, claimed, cc, 0.0f };

                fifo.finishedWrite (size1 + size2);
            }

            continue;
        }

        const auto target = ccToParam[(size_t) cc].load (std::memory_order_relaxed);

        if (target < 0)
            continue;

        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 > 0)
            events[(size_t) start1] = { target, -1, cc, value };

        fifo.finishedWrite (size1 + size2);
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

            // One CC drives one parameter: drop any previous owner of this CC,
            // and any previous CC for this parameter.
            for (auto& slot : ccToParam)
                if (slot.load (std::memory_order_relaxed) == index)
                    slot.store (-1, std::memory_order_relaxed);

            ccToParam[(size_t) e.cc].store (index, std::memory_order_relaxed);
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

    return learnCompleted;
}

//==============================================================================
void MidiLearnManager::startLearning (const juce::String& parameterID)
{
    const auto index = indexOf (parameterID);

    if (index >= 0)
        learnTarget.store (index, std::memory_order_relaxed);
}

void MidiLearnManager::stopLearning()
{
    learnTarget.store (-1, std::memory_order_relaxed);
}

juce::String MidiLearnManager::getLearningParameterID() const
{
    const auto index = learnTarget.load (std::memory_order_relaxed);

    return juce::isPositiveAndBelow (index, parameterIDs.size()) ? parameterIDs[index] : juce::String();
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

    for (auto& slot : ccToParam)
        if (slot.load (std::memory_order_relaxed) == index)
            slot.store (-1, std::memory_order_relaxed);

    ccToParam[(size_t) cc].store (index, std::memory_order_relaxed);
}

void MidiLearnManager::clearMapping (const juce::String& parameterID)
{
    const auto index = indexOf (parameterID);

    if (index < 0)
        return;

    for (auto& slot : ccToParam)
        if (slot.load (std::memory_order_relaxed) == index)
            slot.store (-1, std::memory_order_relaxed);
}

void MidiLearnManager::clearAllMappings()
{
    learnTarget.store (-1, std::memory_order_relaxed);

    for (auto& slot : ccToParam)
        slot.store (-1, std::memory_order_relaxed);
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
