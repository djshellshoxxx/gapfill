#pragma once

// Cross-thread payloads (docs/dev/02-FOUNDATIONS.md F0-4).
//
//   commands    message thread -> audio thread, drained once per block
//   fillEvents  audio thread   -> message thread, drained by the 15 Hz timer
//
// Every payload is trivially copyable. One writer per queue: only the message
// thread pushes commands; only the audio thread pushes fill events.

#include "SchedulerTypes.h"
#include "SpscQueue.h"

#include <atomic>
#include <cstdint>

namespace gapfill
{

//==============================================================================
/** Permanent, append-only values shared by GUI buttons, MIDI (X-1) and tests
    (docs/dev/12 section 2.3). Never renumber. Panic ALSO has an atomic-flag
    path that bypasses the queue. */
enum class CommandType : std::uint8_t
{
    None = 0,
    Preview = 1, LikeLatest = 2, LikeRecipe = 3, LockLatest = 4, LockRecipe = 5,
    Unlock = 6, Recapture = 7, ReleaseCapture = 8, Panic = 9, Recover = 10,
    SceneGoto = 11, SceneNext = 12, ScenePrev = 13, SceneCancel = 14,
    PresetNext = 15, PresetPrev = 16, StutterStorm = 17, Roulette = 18, FillNow = 19,
    MomentaryStutter = 20, MomentaryFreeze = 21, MomentaryReverse = 22, MomentaryTape = 23,
    SpectralCapture = 24, SpectralRelease = 25, GrooveLearn = 26, GrooveReset = 27,
    AbSwap = 28, EventRecordToggle = 29, PreviewRecipe = 30,
    SkipLatest = 31, SkipRecipe = 32         // F-X05 (WP7): ratings; handled on the message thread
};

struct Command
{
    CommandType type = CommandType::None;
    std::uint8_t velocity = 0;
    std::int16_t index = 0;        ///< history slot or similar, meaning depends on `type`
    FillRecipe recipe;             ///< payload of the *Recipe commands
    std::uint32_t issuedMs = 0;    ///< juce::Time::getMillisecondCounter() at push; 0 = unstamped (never stale)
};

//==============================================================================
enum class FillEventType : std::uint8_t
{
    None = 0,
    Started,       ///< a fill started; `recipe` describes it
    Rejected,      ///< a gap was evaluated and not filled; `reason` says why
    // Timeline events (F-M03). Only produced while a visualiser or recorder listens.
    Onset = 3,     ///< a protected event (transient) was detected
    GapOpen = 4,   ///< the gap state machine entered a gap
    FillEnd = 5,   ///< a fill voice finished
    // Values reserved for other tickets so they never collide: E-1 Preview = 6,
    // F-L07 SceneApplied = 7, N-USE-a Panic = 8 / Recover = 9.
    Preview = 6,   ///< E-1: an audition voice started (`pos`/`lengthSamples` locate it); never in history or the outcome log
    SceneApplied = 7, Panic = 8, Recover = 9,
    // WP4 (C-5 Fill Lock): kept clear of the values reserved above.
    LockSet = 20,      ///< the audio thread stored `recipe` as the Fill Lock recipe
    LockCleared = 21   ///< the audio thread forgot the locked recipe (Unlock command)
};

/** True for the two events that describe an evaluated gap (the GapOutcomeLog
    and the Fix-it rules only ever see these). */
constexpr bool isGapOutcome (FillEventType t) noexcept
{
    return t == FillEventType::Started || t == FillEventType::Rejected;
}

struct FillEvent
{
    FillEventType type = FillEventType::None;
    RejectReason reason = RejectReason::None;
    std::uint8_t conf8 = 0;        ///< gap confidence 0..255 at the event (timeline)
    FillRecipe recipe;
    float availMs = 0.0f;
    float needMs = 0.0f;
    std::int64_t pos = 0;          ///< decision sample on the output timeline (= first sample of a started fill)
    std::int32_t lengthSamples = 0;///< Started / Preview: planned length in samples (U-2 export span)
    std::uint32_t generation = 0;  ///< engine gap index (ties the events of one gap together)
    std::uint16_t protHoldMs = 0;  ///< Onset: protection hold that follows the event
    std::uint16_t lookaheadMs = 0; ///< Onset: lookahead window that precedes it
};

//==============================================================================
using CommandQueue = SpscQueue<Command, 16>;

/** Optional audio-thread observer of every fill event (F-D02 event recorder).
    The tap sees events before they enter the queue, so a full queue never hides
    an outcome from a recording. Must be wait-free. */
class FillEventTap
{
public:
    virtual ~FillEventTap() = default;

    /** Audio thread: true while the tap wants rejections and timeline events even
        though no visualiser is listening. */
    virtual bool wantsAll() const noexcept = 0;

    /** Audio thread. */
    virtual void onFillEvent (const FillEvent&) noexcept = 0;
};

/** The audio -> message queue plus its overflow counter. A full queue drops the
    NEWEST event (the producer cannot discard the consumer's oldest without a
    lock) and counts it. */
class FillEventQueue
{
public:
    static constexpr int capacity = 64;

    /** Message thread / prepare: attach or detach the observer. Not while audio runs. */
    void setTap (FillEventTap* t) noexcept   { tap.store (t, std::memory_order_release); }

    /** Audio thread: does an attached tap ask for every event? */
    bool tapWantsAll() const noexcept
    {
        const auto* t = tap.load (std::memory_order_acquire);
        return t != nullptr && t->wantsAll();
    }

    /** Audio thread. */
    void push (const FillEvent& e) noexcept
    {
        if (auto* t = tap.load (std::memory_order_acquire))
            t->onFillEvent (e);

        if (! queue.push (e))
            dropped.fetch_add (1, std::memory_order_relaxed);
    }

    /** Audio thread. Timeline-only events (onsets, gap opens, fill ends): they never
        take the last `reservedForOutcomes` slots, so a burst of onsets cannot push a
        gap outcome or a started recipe out. Dropped ones are counted like any other. */
    void pushOptional (const FillEvent& e) noexcept
    {
        if (auto* t = tap.load (std::memory_order_acquire))
            t->onFillEvent (e);

        if (queue.getNumReady() >= capacity - reservedForOutcomes || ! queue.push (e))
            droppedOptional.fetch_add (1, std::memory_order_relaxed);
    }

    static constexpr int reservedForOutcomes = 16;

    /** Message thread. */
    int pop (FillEvent* dest, int maxItems) noexcept   { return queue.pop (dest, maxItems); }

    /** Outcome / recipe events lost to a full queue. */
    std::uint32_t getDroppedCount() const noexcept     { return dropped.load (std::memory_order_relaxed); }

    /** Timeline-only events skipped to keep room for outcomes (marks the timeline incomplete). */
    std::uint32_t getDroppedOptionalCount() const noexcept { return droppedOptional.load (std::memory_order_relaxed); }
    int getNumReady() const noexcept                   { return queue.getNumReady(); }

    void reset() noexcept
    {
        queue.reset();
        dropped.store (0, std::memory_order_relaxed);
        droppedOptional.store (0, std::memory_order_relaxed);
    }

private:
    SpscQueue<FillEvent, capacity> queue;
    std::atomic<std::uint32_t> dropped { 0 };
    std::atomic<std::uint32_t> droppedOptional { 0 };
    std::atomic<FillEventTap*> tap { nullptr };
};

static_assert (std::is_trivially_copyable_v<Command>);
static_assert (std::is_trivially_copyable_v<FillEvent>);

} // namespace gapfill
