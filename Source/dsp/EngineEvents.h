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
    AbSwap = 28, EventRecordToggle = 29, PreviewRecipe = 30
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
    LockSet,       ///< C-5: the audio thread stored `recipe` as the Fill Lock recipe
    PreviewStarted,///< E-1: an audition voice started; never listed in history or the outcome log
    LockCleared    ///< C-5: the audio thread forgot the locked recipe (Unlock command)
};

struct FillEvent
{
    FillEventType type = FillEventType::None;
    RejectReason reason = RejectReason::None;
    FillRecipe recipe;
    float availMs = 0.0f;
    float needMs = 0.0f;
    std::int64_t pos = 0;          ///< decision sample on the output timeline (= first sample of a started fill)
    std::int32_t lengthSamples = 0;///< Started / PreviewStarted: planned length in samples (U-2 export span)
};

//==============================================================================
using CommandQueue = SpscQueue<Command, 16>;

/** The audio -> message queue plus its overflow counter. A full queue drops the
    NEWEST event (the producer cannot discard the consumer's oldest without a
    lock) and counts it. */
class FillEventQueue
{
public:
    static constexpr int capacity = 64;

    /** Audio thread. */
    void push (const FillEvent& e) noexcept
    {
        if (! queue.push (e))
            dropped.fetch_add (1, std::memory_order_relaxed);
    }

    /** Message thread. */
    int pop (FillEvent* dest, int maxItems) noexcept   { return queue.pop (dest, maxItems); }

    std::uint32_t getDroppedCount() const noexcept     { return dropped.load (std::memory_order_relaxed); }
    int getNumReady() const noexcept                   { return queue.getNumReady(); }

    void reset() noexcept
    {
        queue.reset();
        dropped.store (0, std::memory_order_relaxed);
    }

private:
    SpscQueue<FillEvent, capacity> queue;
    std::atomic<std::uint32_t> dropped { 0 };
};

static_assert (std::is_trivially_copyable_v<Command>);
static_assert (std::is_trivially_copyable_v<FillEvent>);

} // namespace gapfill
