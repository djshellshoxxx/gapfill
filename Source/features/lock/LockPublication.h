#pragma once

#include "../../dsp/FillRecipe.h"
#include "../../dsp/RecipeExtras.h"

#include <array>
#include <atomic>
#include <mutex>

namespace gapfill::lock
{
struct LockSnapshot
{
    bool valid = false;
    FillRecipe recipe {};
    RecipeExtras extras {};
    std::uint64_t restoreEpoch = 0;
    std::uint64_t generation = 0;
};
static_assert (std::is_trivially_copyable_v<LockSnapshot>);

/** Reliable canonical Lock state, independent of the lossy fill-event queue.
    One audio writer publishes without locks, loops or allocation. Non-RT
    restore writers use a private mutex; readers never acquire that mutex.
    All payload words are atomic: bounded coherent reads are race-free even
    when host serializers and the message thread read concurrently. */
class LockPublication
{
public:
    LockPublication() noexcept;
    LockPublication (const LockPublication&) = delete;
    LockPublication& operator= (const LockPublication&) = delete;

    /** Non-RT: publish desired restored state before releasing its new epoch.
        Zero means invalid state/counter exhaustion; no partial state is exposed. */
    std::uint64_t beginRestore (const LockSnapshot&);
    std::uint64_t currentEpoch() const noexcept { return restoreEpoch.load(); }

    /** Audio owner only. Old-epoch updates are ignored; a successful publication
        survives the last callback even when every event queue is full. */
    bool publishAudio (const LockSnapshot&, std::uint64_t acceptedEpoch) noexcept;
    /** Audio: a reliable restore lane, consumed BEFORE ordinary queued commands.
        It does not depend on space in the command queue. */
    bool pendingRestore (std::uint64_t acceptedEpoch, LockSnapshot&) const noexcept;
    /** Any non-mutating reader. Failure means retry/fail complete state capture;
        it must never be interpreted as an intentionally absent Lock subtree. */
    bool tryLatest (LockSnapshot&) const noexcept;

private:
    static constexpr size_t kWords = 10; // canonical 80 bytes, never struct padding
    struct Bank
    {
        bool write (const LockSnapshot&) noexcept; // exactly one writer per bank
        bool readOnce (LockSnapshot&) const noexcept;
        std::atomic<std::uint64_t> sequence { 0 };
        std::array<std::atomic<std::uint64_t>, kWords> words {};
    };
    Bank audio, restored;
    std::mutex restoreWriter;
    std::atomic<std::uint64_t> restoreEpoch { 1 };
    std::atomic<bool> captureFailed { false };
    std::uint64_t audioGeneration = 0; // audio owner only
};
static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
} // namespace gapfill::lock
