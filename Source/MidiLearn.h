#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

namespace gapfill
{

//==============================================================================
/** MIDI-CC learn and mapping for every automatable parameter.

    Threading rules:
      - the audio thread only ever reads the `ccToParam` table and writes to a
        lock-free FIFO. It never touches a String, a ValueTree or the heap.
      - the message thread owns the table and drains the FIFO from a timer, so
        parameter changes are applied where JUCE expects them to happen.

    Mappings are stored by parameter INDEX (the position in allParameterIDs())
    rather than by id, so the audio thread never has to compare strings. The
    saved state still uses the string ids, which keeps it stable across
    versions. */
class MidiLearnManager
{
public:
    MidiLearnManager();

    /** Must be called once, from prepareToPlay or the constructor, before any
        audio runs. Caches the parameter pointers in index order. */
    void connect (juce::AudioProcessorValueTreeState& state);

    //==========================================================================
    // Audio thread.

    /** Scans a block for CC messages. Mapped CCs and a pending learn are pushed
        to the FIFO; nothing is applied here. Real-time safe. */
    void processMidi (const juce::MidiBuffer& midi) noexcept;

    //==========================================================================
    // Message thread.

    /** Applies whatever the audio thread queued. Call from a timer. Returns
        true if a learn completed, so the UI can refresh. */
    bool applyPendingChanges();

    static constexpr int learningTimerHz = 15;
    static constexpr int learningTimeoutSeconds = 10;
    static constexpr int learningTimeoutTicks = learningTimerHz * learningTimeoutSeconds;

    void startLearning (const juce::String& parameterID);
    void stopLearning();

    /** Advances the bounded learn session by one processor timer tick.
        Returns true only when an active learn expires on this call. */
    bool advanceLearningTimeout() noexcept;

    bool isLearning() const noexcept                  { return learnTarget.load() >= 0; }
    int getLearningTicksRemaining() const noexcept    { return learnTicksRemaining.load(); }
    juce::String getLearningParameterID() const;

    /** The CC currently mapped to this parameter, or -1. */
    int getMappedCC (const juce::String& parameterID) const;

    /** The id of the parameter a CC is mapped to, or an empty string (X-1 duplicate check). */
    juce::String getParameterIDForCC (int cc) const;

    /** Maps a CC explicitly (used by state restore and the right-click menu). */
    void setMapping (const juce::String& parameterID, int cc);
    void clearMapping (const juce::String& parameterID);
    void clearAllMappings();
    int  getNumMappings() const;

    //==========================================================================
    // Persistence. The mappings live in their own child tree so that adding or
    // removing parameters in a later version cannot corrupt them.

    static constexpr const char* stateType = "MIDIMAP";

    juce::ValueTree toValueTree() const;
    void restoreFromValueTree (const juce::ValueTree& tree);

private:
    int indexOf (const juce::String& parameterID) const;

    static constexpr int numCCs      = 128;
    static constexpr int fifoSize    = 256;

    struct Event
    {
        int   paramIndex;   // parameter to move, or -1 for a completed learn
        int   learnIndex;   // parameter that just claimed `cc` (learn only)
        int   cc;
        float value;        // 0-1
    };

    juce::AudioProcessorValueTreeState* apvts = nullptr;
    juce::StringArray parameterIDs;
    std::vector<juce::RangedAudioParameter*> parameters;

    std::array<std::atomic<int>, numCCs> ccToParam;
    std::array<std::atomic<int>, numCCs> pendingParamIndex;
    std::array<std::atomic<float>, numCCs> pendingValue;
    std::array<std::atomic<bool>, numCCs> pendingDirty;
    std::atomic<int> learnTarget { -1 };
    std::atomic<int> learnTicksRemaining { 0 };

    // The FIFO is only for completed learn events. Normal mapped CC traffic is
    // coalesced per controller above, so a dense automation burst cannot lose
    // the newest value merely because the message thread has not drained yet.
    juce::AbstractFifo fifo { fifoSize };
    std::array<Event, fifoSize> events {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearnManager)
};

//==============================================================================
/** Finds the MIDI-learn manager belonging to the processor driving `state`,
    or nullptr if that processor does not have one (the test harness, say).

    Implemented in PluginProcessor.cpp so that the GUI controls can reach the
    manager without having to include the whole processor. */
MidiLearnManager* findMidiLearnManager (juce::AudioProcessorValueTreeState& state);

} // namespace gapfill
