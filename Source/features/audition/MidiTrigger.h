#pragma once

// E-1 / U-3: MIDI triggers for Fill Audition.
//
// Contract
//  * scan() runs on the audio thread, once per block, over the raw MidiBuffer
//    bytes (no MidiMessage objects, no allocation, no locks). A NoteOn
//    (velocity > 0) matching the trigger note and channel fires; NoteOff is
//    ignored. A CC fires on a rising edge >= 64 and re-arms once it falls
//    below 48 (creative spec section 8). No MIDI is ever output.
//  * Learn is a bounded session (10 s, ticked by the processor's 15 Hz timer):
//    the next NoteOn (note learn) or the next CC (CC learn) becomes the trigger.
//  * Settings are not host-automatable; they are stored in the `<Perf>` subtree
//    and applied from the message thread. The audio thread only reads atomics.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <cstdint>

namespace gapfill::audition
{

class MidiTrigger
{
public:
    enum class Learn : int { None = 0, Note = 1, CC = 2 };

    static constexpr int timerHz        = 15;
    static constexpr int learnSeconds   = 10;
    static constexpr int learnTicks     = timerHz * learnSeconds;
    static constexpr int ccFireAt       = 64;
    static constexpr int ccRearmBelow   = 48;

    //==========================================================================
    // Audio thread

    /** Scans one block. Returns true when a preview should be requested and
        sets `velocity` (1..127; a CC trigger reports 100). */
    bool scan (const juce::MidiBuffer& midi, std::uint8_t& velocity) noexcept;

    //==========================================================================
    // Message thread

    int  getNote() const noexcept          { return note.load (std::memory_order_relaxed); }       ///< -1 = off
    int  getChannel() const noexcept       { return channel.load (std::memory_order_relaxed); }    ///< 0 = any, 1..16
    int  getCC() const noexcept            { return cc.load (std::memory_order_relaxed); }         ///< -1 = none

    void setNote (int n) noexcept          { note.store (n >= 0 && n <= 127 ? n : -1, std::memory_order_relaxed); }
    void setChannel (int c) noexcept       { channel.store (c >= 1 && c <= 16 ? c : 0, std::memory_order_relaxed); }
    void setCC (int c) noexcept            { cc.store (c >= 0 && c <= 127 ? c : -1, std::memory_order_relaxed); ccArmed.store (true, std::memory_order_relaxed); }

    void startLearn (Learn kind) noexcept;
    void stopLearn() noexcept;
    Learn getLearn() const noexcept        { return (Learn) learnKind.load (std::memory_order_relaxed); }
    int  getLearnTicksRemaining() const noexcept   { return learnLeft.load (std::memory_order_relaxed); }

    /** One processor-timer tick. Returns true when an active learn expired. */
    bool advanceLearnTimeout() noexcept;

    void reset() noexcept;                 ///< note off, any channel, no CC, no learn

    //==========================================================================
    // Persistence (`<Perf>`): plain integer attributes, range-checked on load.
    static bool isValid (const juce::ValueTree& perf);
    void writeTo (juce::ValueTree& perf) const;
    void readFrom (const juce::ValueTree& perf) noexcept;

private:
    std::atomic<int> note { -1 }, channel { 0 }, cc { -1 };
    std::atomic<int> learnKind { 0 }, learnLeft { 0 };
    std::atomic<bool> ccArmed { true };
};

} // namespace gapfill::audition
