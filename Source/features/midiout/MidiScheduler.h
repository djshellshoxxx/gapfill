#pragma once

// F-L11 additive event scheduler. Pure bounded state/packet model; the JUCE MidiBuffer adapter is a separate TU.
// One audio-thread owner, except requestFlush(). No allocation, locks, wall clock, or RNG.
//
// beginBlock -> consume/consumeEarlyHits (any chunk order) -> render/finish once. Events are merged by wire time
// before updating note ownership, so early Hits and output-aligned Gap/Fill events cannot invalidate future notes.
// Notes are uniquely owned by (channel,note), including aliases between event types. Offs precede retriggers.
// A caller merging legacy handoff offs supplies the remaining per-block budget. The old finish signature remains
// clearing by default; use clearInput=false only after the host input has been consumed/cleared exactly once.

#include "EngineEvents.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

namespace juce { class MidiBuffer; }

namespace gapfill::midiout
{

inline constexpr int kSchedulerMaxEvents = 32;

struct MidiOutSettings
{
    bool on = false;
    int channel = 10;
    bool hitOn = true, gapOn = false, fillOn = false;
    int noteHit = 36, noteGap = 37, noteFill = 38;
    bool velFollow = true;
    float hitLenMs = 60.0f;
    int cc = 0;
    bool early = false;
    int offsetSamples = 0;
};

struct MidiPacket
{
    int offset = 0;
    std::uint8_t status = 0, data1 = 0, data2 = 0;
};

class MidiScheduler
{
    static_assert (std::atomic<bool>::is_always_lock_free, "MIDI flush requests must never lock the audio thread");
public:
    /** Audio stopped: retain old note identities for a safe flush after a rate change. */
    void prepare (double sampleRate) noexcept;
    void requestFlush() noexcept { flushRequested.store (true, std::memory_order_release); }
    void reset() noexcept { requestFlush(); }
    /** Explicit flush suppresses new detector notes for this block; the caller also gates sustained panic/stop. */
    void beginBlock (const MidiOutSettings&, std::int64_t outputStart, int samples, std::int64_t inputStart,
                     int eventBudget = kSchedulerMaxEvents) noexcept;
    void consume (const EngineEventBuffer&) noexcept;
    void consumeEarlyHits (const EngineEventBuffer&) noexcept;
    int render (std::array<MidiPacket, kSchedulerMaxEvents>&) noexcept;
    int finish (juce::MidiBuffer&, bool clearInput = true) noexcept;

    std::uint64_t getDroppedCount() const noexcept { return dropped; }
    bool hasSoundingNotes() const noexcept;

private:
    enum Source : int { HitSource = 0, GapSource, FillSource, NumSources };
    struct Active
    {
        bool on = false;
        int channel = 1, note = 0;
        std::int64_t start = 0, deadline = std::numeric_limits<std::int64_t>::max();
    };
    struct Queued
    {
        OutEvent event;
        std::int64_t wireTime = 0;
        int order = 0;
    };

    static std::int64_t add (std::int64_t, std::int64_t) noexcept;
    void queue (const OutEvent&, bool early) noexcept;
    void process (const Queued&) noexcept;
    void noteOn (Source, int note, float strength, std::int64_t time, std::int64_t duration) noexcept;
    bool noteOff (int source, std::int64_t time) noexcept;
    void fireDue (std::int64_t) noexcept;
    bool packet (std::int64_t, int status, int data1, int data2) noexcept;
    int velocity (float strength) const noexcept;

    double rate = 48000.0;
    MidiOutSettings current;
    std::int64_t outputStart = 0, inputStart = 0, previousEnd = 0;
    int samples = 0, budget = kSchedulerMaxEvents;
    bool previousValid = false, suppressed = false;
    std::uint8_t flushMask = 0;
    std::array<Active, NumSources> active {};
    std::array<Queued, EngineEventBuffer::capacity> queued {};
    int queuedCount = 0, nextOrder = 0;
    std::array<MidiPacket, kSchedulerMaxEvents> pending {};
    int pendingCount = 0;
    bool ccValid = false;
    int lastCc = 0;
    std::int64_t lastCcTime = 0;
    std::uint64_t dropped = 0;
    std::atomic<bool> flushRequested { false };
};

}
