#pragma once

// F-L11 MIDI output from gap / fill events (docs/dev/12 F-L11).
//
// Parameters: midiOutMode (Off, Gaps, Fills, Both), midiOutNote (default 36),
// midiOutChan (default 10).
//   Gaps  : NoteOn on `midiOutNote` when a gap opens (velocity = detector
//           confidence x 127, min 1), NoteOff when it closes.
//   Fills : NoteOn / NoteOff on `midiOutNote + 1` for every natural fill
//           (velocity = the fill's intensity, 1..127).
//   Both  : both.
// Time-stamps are OUTPUT-domain sample offsets inside the host block, i.e. aligned
// with the delayed audio (the engine's output timeline). `offsetSamples` (Options
// midiOutOffsetMs, -100..+100 ms) shifts them for hosts that do not delay MIDI by
// the reported latency; offsets are clamped into the current block.
//
// Bounds: at most 16 events per host block (excess NoteOns are dropped and counted;
// a NoteOff for a sounding note is never dropped). Stuck-note guard: every sounding
// note gets its NoteOff on panic, bypass, transport stop / jump, mode or note change,
// and (at the first block) after prepare / release. The emitter never allocates: all
// storage is fixed, and the host MidiBuffer is cleared and written in place (the
// JUCE wrappers pre-size it to 2048 bytes, an event here is < 12).
//
// The incoming MIDI (learn CCs, commands) is NEVER echoed: when the emitter is
// active the buffer is cleared before our events are written.
//
// Build option GAPFILL_MIDI_OUT (default OFF) decides whether the plug-in DECLARES
// MIDI output (producesMidi / NEEDS_MIDI_OUTPUT). Unit-test builds always run the
// emitter so it can be tested without the declaration.

#include "../../dsp/EngineTransitions.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>

#ifndef GAPFILL_MIDI_OUT
 #define GAPFILL_MIDI_OUT 0
#endif

namespace gapfill::midiout
{

enum Mode { ModeOff = 0, ModeGaps, ModeFills, ModeBoth, NumModes };

inline constexpr bool kDeclared = GAPFILL_MIDI_OUT != 0;

#if defined (GAPFILL_UNIT_TESTS)
inline constexpr bool kActive = true;
#else
inline constexpr bool kActive = kDeclared;
#endif

inline constexpr int kMaxEventsPerBlock = 16;
inline constexpr int kMaxOffsetMs = 100;

struct Settings
{
    int mode = ModeOff;
    int note = 36;           ///< Gaps; fills use note + 1 (clamped to 127)
    int channel = 10;        ///< 1..16
    int offsetSamples = 0;   ///< output alignment shift

    bool gaps() const noexcept    { return mode == ModeGaps || mode == ModeBoth; }
    bool fills() const noexcept   { return mode == ModeFills || mode == ModeBoth; }
};

class MidiOutEmitter
{
public:
    /** Audio thread, once per host block: handles mode / note changes and a requested flush. */
    void beginBlock (const Settings&, int hostBlockSamples) noexcept;

    /** Audio thread, once per chunk, after the engine ran. `chunkOutputStart` is the output
        position of host sample `chunkStartSample`. Transitions before that point are ignored. */
    void consume (const TransitionLog&, std::int64_t chunkOutputStart, int chunkStartSample,
                  int chunkSamples) noexcept;

    /** Any thread: all sounding notes get their NoteOff at the start of the next block. */
    void requestFlush() noexcept   { flushRequested.store (true, std::memory_order_relaxed); }

    /** Audio thread, once per host block: writes the events (clearing the incoming MIDI first)
        when anything is pending or the emitter is enabled. Returns the number of events written. */
    int finish (juce::MidiBuffer&) noexcept;

    bool isEnabled() const noexcept       { return current.mode != ModeOff; }
    bool hasSoundingNotes() const noexcept { return gapOn || fillsOn > 0; }
    int  getDroppedCount() const noexcept { return dropped; }
    int  getFillsSounding() const noexcept { return fillsOn; }

    /** Message / test thread while the audio is stopped. */
    void resetCounters() noexcept   { dropped = 0; }

private:
    struct Msg
    {
        int offset = 0;
        std::uint8_t status = 0, d1 = 0, d2 = 0;
        int order = 0;
    };

    bool push (int offset, bool on, int note, int channel, int velocity, bool force) noexcept;
    void flushAll (int offset) noexcept;
    int  mapOffset (std::int64_t pos, std::int64_t chunkOutputStart, int chunkStartSample) const noexcept;

    static constexpr int kPending = 48;
    std::array<Msg, kPending> pending {};
    int numPending = 0;
    int order = 0;
    int emittedThisBlock = 0;
    int hostSamples = 0;

    Settings current;
    bool gapOn = false;
    int  gapNote = 0, gapChan = 1;
    int  fillsOn = 0;
    int  fillNote = 0, fillChan = 1;
    int  dropped = 0;
    std::atomic<bool> flushRequested { true };      // the first block after construction / prepare starts clean
};

} // namespace gapfill::midiout
