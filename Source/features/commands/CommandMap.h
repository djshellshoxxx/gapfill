#pragma once

// X-1 MIDI command map (docs/dev/12 section 3, X-1).
//
// Binds MIDI messages (CC, Note, Program Change) to COMMANDS (CommandType,
// docs/dev/12 section 2.3). It complements MidiLearnManager (CC -> parameter),
// which is unchanged.
//
//   BindingModel   message thread. Owns the table (<= 48 bindings), validates
//                  edits, serialises the <Cmd> subtree, publishes snapshots.
//   Scanner        audio thread. One pass over the MidiBuffer per block, fixed
//                  arrays, no allocation. Produces Events; learn captures the
//                  next message and hands it back through a bounded queue.
//
// The two sides only share a TripleBuffer<BindingTable> and the learn queue.

#include "../../dsp/EngineEvents.h"
#include "../../dsp/SpscQueue.h"
#include "../common/TripleBuffer.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <cstdint>

namespace gapfill::commands
{

inline constexpr int kMaxBindings = 48;
inline constexpr int kMaxMidiEventsPerBlock = 128;
inline constexpr int kMaxCommandEventsPerBlock = 32;

enum class MsgType : std::uint8_t { CC = 0, Note = 1, ProgramChange = 2 };
enum class BindMode : std::uint8_t { Trigger = 0, Momentary = 1, Toggle = 2 };

struct Binding
{
    CommandType   cmd = CommandType::None;
    std::int16_t  arg = 0;                   ///< e.g. scene index for SceneGoto
    MsgType       type = MsgType::CC;
    std::uint8_t  channel = 0;               ///< 0 = any, 1..16
    std::uint8_t  number = 0;                ///< CC number, note number or program number (0..127)
    BindMode      mode = BindMode::Trigger;
    std::uint8_t  threshold = 64;            ///< CC rising edge; re-arms below threshold - 8
};

struct BindingTable
{
    std::int32_t count = 0;
    std::uint8_t pcSceneEnabled = 0;         ///< N-VAL: Program Change 0..63 selects scene n
    std::uint8_t pcSceneChannel = 0;         ///< 0 = any
    std::uint8_t pad[2] {};
    Binding      bindings[kMaxBindings] {};
};

/** A command raised by MIDI. `on` is true for a press / momentary-on / toggled
    on, false for a momentary release / toggled off. */
struct Event
{
    CommandType  type = CommandType::None;
    std::int16_t arg = 0;
    bool         on = true;
    std::int32_t sampleOffset = 0;
};

/** True when CommandType value is one of the permanent ids 1..30. */
bool isKnownCommand (CommandType) noexcept;

/** Human-readable command name for lists and status text. */
const char* commandName (CommandType) noexcept;

bool isValid (const Binding&) noexcept;

/** Two bindings react to the same incoming message. */
bool sameMessage (const Binding&, const Binding&) noexcept;

//==============================================================================
/** Message-thread model of the table. */
class BindingModel
{
public:
    enum class AddResult { Ok, Full, Invalid, Duplicate };

    BindingModel();

    /** Adds a binding. `Duplicate` means another binding already reacts to the
        same message; its index is written to `ownerIndex`. */
    AddResult add (const Binding&, int* ownerIndex = nullptr);
    void removeAt (int index);
    void clear();

    int  size() const noexcept                       { return table.count; }
    const Binding& get (int index) const noexcept    { return table.bindings[index]; }
    const BindingTable& getTable() const noexcept    { return table; }

    /** Index of the binding that reacts to the same message as `b`, or -1. */
    int findOwner (const Binding& b) const noexcept;

    /** Index of the first binding of `cmd`/`arg`, or -1. */
    int findCommand (CommandType cmd, int arg) const noexcept;

    void setProgramChangeScenes (bool enabled, int channel) noexcept;

    /** Latest published snapshot for the audio thread. */
    TripleBuffer<BindingTable>& publisher() noexcept { return pub; }
    void publish() noexcept                          { pub.publish (table); }

    std::string getFeedbackDevice() const            { return feedbackDevice; }
    void setFeedbackDevice (const std::string& s)    { feedbackDevice = s.substr (0, 96); }

    //------------------------------------------------------------------------
    // <Cmd> subtree (<= 3 KiB). Invalid bindings are dropped one by one, the
    // first binding of a duplicated message wins, more than 48 are truncated.
    static constexpr const char* subtreeName = "Cmd";

    juce::ValueTree toValueTree() const;
    static bool validateTree (const juce::ValueTree&);
    void restore (const juce::ValueTree&);

private:
    BindingTable table;
    TripleBuffer<BindingTable> pub;
    std::string feedbackDevice;
};

//==============================================================================
/** What the user is teaching the map. */
struct LearnTarget
{
    CommandType  cmd = CommandType::None;
    std::int16_t arg = 0;
    BindMode     mode = BindMode::Trigger;
};

/** Audio-thread scanner. */
class Scanner
{
public:
    Scanner() noexcept { for (auto& a : armed) a = true; }

    /** Audio thread: adopts a new table. A momentary binding that was held when
        the table changed emits its release into `out`. */
    int adoptTable (const BindingTable&, Event* out, int maxOut) noexcept;

    /** Audio thread. Scans the block once and writes Events in message order.
        At most kMaxMidiEventsPerBlock messages are inspected; the rest are
        counted in getDroppedMidi(). Never allocates. */
    int process (const juce::MidiBuffer&, Event* out, int maxOut) noexcept;

    /** Releases every held momentary binding (panic / bypass / stop). */
    int releaseAll (Event* out, int maxOut) noexcept;

    //------------------------------------------------------------------------
    // Learn (message thread arms, audio thread captures).
    void armLearn (const LearnTarget&) noexcept;
    void cancelLearn() noexcept;
    bool isLearning() const noexcept                 { return learnPacked.load (std::memory_order_relaxed) != 0; }

    /** Message thread. Returns a binding captured by the audio thread, if any. */
    bool popLearned (Binding& out) noexcept          { return learned.pop (&out, 1) == 1; }

    std::uint32_t getDroppedMidi() const noexcept    { return droppedMidi.load (std::memory_order_relaxed); }

    const BindingTable& getActiveTable() const noexcept { return table; }

private:
    void fire (int bindingIndex, bool on, int offset, Event* out, int& n, int maxOut) const noexcept;

    BindingTable table;
    bool armed[kMaxBindings] {};
    bool held[kMaxBindings] {};
    bool toggled[kMaxBindings] {};

    std::atomic<std::uint32_t> learnPacked { 0 };
    SpscQueue<Binding, 4> learned;
    std::atomic<std::uint32_t> droppedMidi { 0 };
};

} // namespace gapfill::commands
