#pragma once

// F-X06 bounded process-local membership. All shared fields are atomics: readers
// never race a writer's plain data, even when a snapshot copy must be retried.
// A callback admits its complete PPQ range before reading the roster. Publishing
// CASes that same admission word, placing activation beyond every admitted range.

#include "Deterministic.h"

#include <atomic>
#include <bit>

namespace gapfill::coop::deterministic
{

inline constexpr double kTicksPerBeat = 1048576.0;
inline constexpr std::int64_t kMinTick = -(std::int64_t (1) << 43);
inline constexpr std::int64_t kMaxTick = (std::int64_t (1) << 43) - 1;

struct Handle { std::uint64_t token = 0; int cell = -1; std::uint8_t group = 0; };

struct Plan
{
    Snapshot snapshot;
    Grid grid = Grid::Bar;
    double beats = 4.0;
    std::int64_t activationTick = kMinTick;
};

struct View
{
    Plan current, previous;
    std::uint64_t selfToken = 0;
    bool forceCurrent = false;
    // Derived cache belongs to ONE audio owner, like the view itself. Shared
    // immutable Snapshot election stays pure; no UI/thread reads this cache.
    struct Cache
    {
        std::uint64_t generation = 0, token = 0;
        std::int64_t slotIndex = 0;
        std::uint8_t selfSlot = 0, ownerSlot = 0;
    };
    mutable Cache cache;
};

struct Decision
{
    bool ready = false, owner = false;
    std::uint8_t selfSlot = 0, ownerSlot = 0;
    std::uint64_t generation = 0;
    Scale scale;
};

inline Decision decisionAt (const View& view, double ppq, bool transportValid, float yieldPercent) noexcept
{
    Decision result;
    if (! transportValid || ! std::isfinite (ppq))
        return result;                            // unavailable transport is neutral
    const auto& plan = view.forceCurrent || (ppq + kPpqRoundingBeats) * kTicksPerBeat >= (double) view.current.activationTick
                     ? view.current : view.previous;
    std::int64_t index = 0;
    if (! slotIndexForBeats (ppq, plan.beats, index))
        return result;
    if (view.cache.generation == plan.snapshot.generation && view.cache.generation != 0
        && view.cache.token == view.selfToken && view.cache.slotIndex == index)
    {
        result.selfSlot = view.cache.selfSlot;
        result.ownerSlot = view.cache.ownerSlot;
    }
    else
    {
        if (! valid (plan.snapshot))
            return result;
        for (int i = 0; i < plan.snapshot.count; ++i)
            if (plan.snapshot.members[(std::size_t) i].token == view.selfToken)
                result.selfSlot = plan.snapshot.members[(std::size_t) i].slot;
        if (result.selfSlot == 0)
            return result;                        // joining member waits for the shared boundary
        result.ownerSlot = ownerSlot (plan.snapshot, index);
        view.cache = { plan.snapshot.generation, view.selfToken, index, result.selfSlot, result.ownerSlot };
    }
    result.owner = result.ownerSlot == result.selfSlot;
    result.ready = true;
    result.generation = plan.snapshot.generation;
    result.scale = yieldFor (result.owner, yieldPercent);
    return result;
}

class Registry
{
public:
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert (std::atomic<std::int64_t>::is_always_lock_free);

    Handle claim (std::uint8_t group, std::uint8_t slot, std::uint8_t priority, Grid grid) noexcept
    {
        if (! validConfig (group, slot, priority, grid))
            return {};
        auto token = nextToken.load (std::memory_order_relaxed);
        bool allocated = false;
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            if (token == 0 || token > kMaxToken)
                return {};                        // saturate; never wrap/reuse a lifetime identity
            if (nextToken.compare_exchange_weak (token, token + 1, std::memory_order_relaxed))
            {
                allocated = true;
                break;
            }
        }
        if (! allocated)
            return {};                            // exhausted lifetime identity: never wrap/reuse
        for (int i = 0; i < kCapacity; ++i)
        {
            std::uint64_t expected = 0;
            auto& cell = cells[(std::size_t) i];
            if (! cell.config.compare_exchange_strong (expected, kClaiming, std::memory_order_acq_rel))
                continue;
            cell.start.store (kMinTick, std::memory_order_relaxed);
            cell.end.store (kMinTick, std::memory_order_relaxed);
            cell.forced.store (0, std::memory_order_relaxed);
            cell.config.store (pack (token, group, slot, priority, grid), std::memory_order_release);
            changed();
            return { token, i, group };
        }
        return {};
    }

    bool owns (const Handle& h) const noexcept
    {
        return h.cell >= 0 && h.cell < kCapacity && h.token != 0
            && tokenOf (cells[(std::size_t) h.cell].config.load (std::memory_order_acquire)) == h.token;
    }

    bool update (Handle& h, std::uint8_t group, std::uint8_t slot, std::uint8_t priority, Grid grid) noexcept
    {
        if (! owns (h) || ! validConfig (group, slot, priority, grid))
            return false;
        auto& config = cells[(std::size_t) h.cell].config;
        auto expected = config.load (std::memory_order_acquire);
        const auto desired = pack (h.token, group, slot, priority, grid);
        if (expected == desired)
        {
            h.group = group;
            return true;
        }
        if (tokenOf (expected) != h.token || ! config.compare_exchange_strong
            (expected, desired, std::memory_order_acq_rel))
            return false;
        h.group = group;
        changed();
        return true;
    }

    void release (Handle& h) noexcept
    {
        if (owns (h))
        {
            auto& config = cells[(std::size_t) h.cell].config;
            auto expected = config.load (std::memory_order_acquire);
            if (tokenOf (expected) == h.token && config.compare_exchange_strong (expected, 0, std::memory_order_acq_rel))
                changed();
        }
        h = {};
    }

    int liveCount() const noexcept
    {
        int count = 0;
        for (const auto& cell : cells)
        {
            const auto config = cell.config.load (std::memory_order_acquire);
            count += config != 0 && config != kClaiming ? 1 : 0;
        }
        return count;
    }

    std::uint8_t lowestFreeSlot (std::uint8_t group, std::uint64_t exceptToken = 0) const noexcept
    {
        std::uint32_t used = 0;
        for (const auto& cell : cells)
        {
            const auto config = cell.config.load (std::memory_order_acquire);
            if (isMember (config, group) && tokenOf (config) != exceptToken)
                used |= std::uint32_t (1) << (slotOf (config) - 1);
        }
        for (int i = 0; i < kCapacity; ++i)
            if ((used & (std::uint32_t (1) << i)) == 0)
                return (std::uint8_t) (i + 1);
        return 0;
    }

    /** Only the later claimant repairs a duplicated musical slot; the earlier
        claimant keeps it. The message-thread adapter synchronizes a returned
        slot to the host parameter. No processor pointer crosses the registry. */
    std::uint8_t repairSlot (std::uint8_t group, std::uint64_t token) noexcept
    {
        for (auto& cell : cells)
        {
            auto config = cell.config.load (std::memory_order_acquire);
            if (! isMember (config, group) || tokenOf (config) != token)
                continue;
            bool earlierDuplicate = false;
            for (const auto& other : cells)
            {
                const auto value = other.config.load (std::memory_order_acquire);
                earlierDuplicate = earlierDuplicate || (isMember (value, group) && tokenOf (value) < token
                                                       && slotOf (value) == slotOf (config));
            }
            if (! earlierDuplicate)
                return 0;
            const auto slot = lowestFreeSlot (group, token);
            if (slot != 0 && cell.config.compare_exchange_strong (config,
                pack (token, group, slot, priorityOf (config), gridOf (config)), std::memory_order_acq_rel))
            {
                changed();
                return slot;
            }
            return 0;
        }
        return 0;
    }

    /** One bounded publication attempt. A duplicate slot, busy writer, changing
        input, still-needed predecessor or competing admission leaves the old
        coherent plan intact; a later callback/message tick may try again. */
    bool maintain (std::uint8_t group, double beatsPerBar) noexcept
    {
        if (group < 1 || group > 8)
            return false;
        auto& state = groups[(std::size_t) group - 1];
        const auto revision = configRevision.load (std::memory_order_acquire);
        const double bar = std::isfinite (beatsPerBar) && beatsPerBar > 0.0 ? beatsPerBar : 4.0;
        const auto barBits = std::bit_cast<std::uint64_t> (bar);
        if (revisionsExhausted.load (std::memory_order_relaxed)
            || (state.reconciledRevision.load (std::memory_order_acquire) == revision
                && state.barBits.load (std::memory_order_relaxed) == barBits))
            return false;
        if (state.writer.test_and_set (std::memory_order_acquire))
            return false;
        struct Unlock { std::atomic_flag& flag; ~Unlock() { flag.clear (std::memory_order_release); } } unlock { state.writer };
        auto clock = state.clock.load (std::memory_order_acquire);
        const auto generation = generationOf (clock);
        if (generation == kGenerationMask)
            return false;                          // no generation ABA after exhaustion
        Plan current;
        if (generation != 0 && ! copyPlan (state, generation, current))
            return false;

        std::array<std::uint64_t, kCapacity> configs;
        Plan next;
        next.snapshot.group = group;
        next.snapshot.generation = generation + 1;
        std::uint8_t firstSlot = 255;
        bool allForced = generation != 0;
        std::int64_t actualEnd = kMinTick;
        for (int i = 0; i < kCapacity; ++i)
        {
            const auto config = cells[(std::size_t) i].config.load (std::memory_order_acquire);
            configs[(std::size_t) i] = config;
            if (! isMember (config, group))
                continue;
            auto& member = next.snapshot.members[(std::size_t) next.snapshot.count++];
            member = { tokenOf (config), slotOf (config), priorityOf (config) };
            if (member.slot < firstSlot)
            {
                firstSlot = member.slot;
                next.grid = gridOf (config);
            }
            allForced = allForced && cells[(std::size_t) i].forced.load (std::memory_order_relaxed) >= generation;
            actualEnd = std::max (actualEnd, cells[(std::size_t) i].end.load (std::memory_order_relaxed));
        }
        if (! valid (next.snapshot))
            return false;
        for (int i = 0; i < kCapacity; ++i)
            if (configs[(std::size_t) i] != cells[(std::size_t) i].config.load (std::memory_order_acquire))
                return false;
        std::sort (next.snapshot.members.begin(), next.snapshot.members.begin() + next.snapshot.count,
                   [] (const Member& a, const Member& b) { return a.slot < b.slot; });
        switch (next.grid)
        {
            case Grid::Beat: next.beats = 1.0; break;
            case Grid::TwoBeats: next.beats = 2.0; break;
            case Grid::Bar: next.beats = bar; break;
            case Grid::TwoBars: next.beats = bar * 2.0; break;
            case Grid::FourBars: next.beats = bar * 4.0; break;
        }
        if (generation != 0 && sameMembership (current, next))
        {
            state.barBits.store (barBits, std::memory_order_relaxed);
            state.reconciledRevision.store (revision, std::memory_order_release);
            return false;
        }
        // A third version cannot displace a predecessor that a current member
        // still needs. Departed tokens cannot keep a group stuck forever.
        if (generation != 0 && current.activationTick != kMinTick)
            for (int i = 0; i < kCapacity; ++i)
                if (isMember (configs[(std::size_t) i], group))
                    for (int j = 0; j < current.snapshot.count; ++j)
                        if (tokenOf (configs[(std::size_t) i]) == current.snapshot.members[(std::size_t) j].token
                            && cells[(std::size_t) i].forced.load (std::memory_order_relaxed) < generation
                            && cells[(std::size_t) i].start.load (std::memory_order_relaxed) < current.activationTick)
                            return false;

        bool continuingMember = false;
        for (int i = 0; i < next.snapshot.count; ++i)
            for (int j = 0; j < current.snapshot.count; ++j)
                continuingMember = continuingMember || next.snapshot.members[(std::size_t) i].token
                    == current.snapshot.members[(std::size_t) j].token;
        // A completely departed cohort cannot retain a previous owner or an
        // old project's PPQ frontier. New lifetime tokens register before their
        // first render, so this roster is the new cohort's initial plan. This
        // also avoids requiring all hosts' first Start callbacks to run first.
        const bool freshCohort = generation == 0 || ! continuingMember;
        const auto admitted = freshCohort || allForced ? actualEnd : tickOf (clock);
        if (! freshCohort && admitted != kMinTick)
        {
            const auto boundary = (std::floor (((double) admitted / kTicksPerBeat) / next.beats) + 2.0) * next.beats;
            if (! toTick (boundary, true, next.activationTick))
                return false;
        }
        writePlan (state, next);
        if (! state.clock.compare_exchange_strong (clock, encodeClock (admitted, generation + 1),
                                                   std::memory_order_acq_rel))
            return false;
        state.barBits.store (barBits, std::memory_order_relaxed);
        state.reconciledRevision.store (revision, std::memory_order_release);
        return true;
    }

    bool latest (std::uint8_t group, Plan& out) const noexcept
    {
        if (group < 1 || group > 8)
            return false;
        const auto& state = groups[(std::size_t) group - 1];
        return copyPlan (state, generationOf (state.clock.load (std::memory_order_acquire)), out);
    }

    bool pending (std::uint8_t group) const noexcept
    {
        return group >= 1 && group <= 8 && groups[(std::size_t) group - 1].reconciledRevision.load
            (std::memory_order_acquire) != configRevision.load (std::memory_order_acquire);
    }

    bool sceneDeadline (std::uint8_t group, double& ppq, Plan& plan) const noexcept
    {
        if (group < 1 || group > 8)
            return false;
        const auto& state = groups[(std::size_t) group - 1];
        const auto clock = state.clock.load (std::memory_order_acquire);
        if (tickOf (clock) == kMinTick || ! copyPlan (state, generationOf (clock), plan))
            return false;
        ppq = (std::floor (((double) tickOf (clock) / kTicksPerBeat) / plan.beats) + 2.0) * plan.beats;
        return std::isfinite (ppq) && ppq * kTicksPerBeat > (double) plan.activationTick;
    }

    void noteTransportJump (const Handle& h) noexcept
    {
        if (owns (h) && h.group >= 1 && h.group <= 8)
            cells[(std::size_t) h.cell].forced.store (generationOf (groups[(std::size_t) h.group - 1].clock.load
                (std::memory_order_acquire)), std::memory_order_relaxed);
    }

    /** Admission precedes rendering. Every admission changes the atomic word
        (even behind the group frontier), so a publisher CAS cannot miss it. */
    bool admit (const Handle& h, double ppqStart, double ppqEnd, View& out) noexcept
    {
        std::int64_t start = 0, end = 0;
        if (! owns (h) || h.group < 1 || h.group > 8 || ppqEnd < ppqStart
            || ! toTick (ppqStart, false, start) || ! toTick (ppqEnd, true, end))
            return false;
        auto& cell = cells[(std::size_t) h.cell];
        cell.start.store (start, std::memory_order_relaxed);
        cell.end.store (end, std::memory_order_relaxed);
        auto& state = groups[(std::size_t) h.group - 1];
        auto clock = state.clock.load (std::memory_order_acquire);
        bool admitted = false;
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            if (tickOf (clock) == kMaxTick)
                return false;
            const auto next = encodeClock (std::max (tickOf (clock) + 1, end), generationOf (clock));
            if (state.clock.compare_exchange_weak (clock, next, std::memory_order_acq_rel))
            {
                admitted = true;
                break;
            }
        }
        if (! admitted)
            return false;
        // Atomic payloads + sequence checks prevent data races if an old plane
        // is recycled while this reader is descheduled. Failure stays visible.
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto generation = generationOf (state.clock.load (std::memory_order_acquire));
            if (out.current.snapshot.generation == generation && generation != 0 && out.selfToken == h.token
                && out.current.snapshot.group == h.group)
            {
                out.forceCurrent = cell.forced.load (std::memory_order_relaxed) >= generation;
                return true;                     // private caller cache remains immutable between admissions
            }
            View value;
            if (generation == 0 || ! copyPlan (state, generation, value.current))
                continue;
            if (generation > 1 && ! copyPlan (state, generation - 1, value.previous))
                continue;
            value.selfToken = h.token;
            value.forceCurrent = cell.forced.load (std::memory_order_relaxed) >= generation;
            out = value;
            return true;
        }
        return false;
    }

private:
    static constexpr std::uint64_t kGenerationMask = (std::uint64_t (1) << 20) - 1;
    static constexpr std::uint64_t kMaxToken = (std::uint64_t (1) << 45) - 1;
    static constexpr std::uint64_t kClaiming = ~std::uint64_t (0);
    struct Cell
    {
        std::atomic<std::uint64_t> config { 0 }, forced { 0 };
        std::atomic<std::int64_t> start { kMinTick }, end { kMinTick };
    };
    struct Plane
    {
        std::atomic<std::uint64_t> sequence { 0 }, generation { 0 }, beats { 0 };
        std::atomic<std::int64_t> activation { kMinTick };
        std::atomic<int> count { 0 };
        std::atomic<std::uint8_t> grid { 0 };
        std::array<std::atomic<std::uint64_t>, kCapacity> rows {};
    };
    struct GroupState
    {
        std::atomic_flag writer = ATOMIC_FLAG_INIT;
        std::atomic<std::uint64_t> clock { 0 };
        std::atomic<std::uint64_t> reconciledRevision { 0 }, barBits { 0 };
        std::array<Plane, 3> planes;
    };
    std::atomic<std::uint64_t> nextToken { 1 };
    std::atomic<std::uint64_t> configRevision { 1 };
    std::atomic<bool> revisionsExhausted { false };
    std::array<Cell, kCapacity> cells;
    std::array<GroupState, 8> groups;

    void changed() noexcept
    {
        if (configRevision.fetch_add (1, std::memory_order_release) == std::numeric_limits<std::uint64_t>::max())
            revisionsExhausted.store (true, std::memory_order_release);
    }

    static bool validConfig (std::uint8_t group, std::uint8_t slot, std::uint8_t priority, Grid grid) noexcept
    {
        return group >= 1 && group <= 8 && slot >= 1 && slot <= kCapacity && priority <= 100
            && (int) grid <= (int) Grid::FourBars;
    }
    static std::uint64_t pack (std::uint64_t token, std::uint8_t group, std::uint8_t slot,
                               std::uint8_t priority, Grid grid) noexcept
    {
        return (token << 19) | ((std::uint64_t) grid << 16) | ((std::uint64_t) priority << 9)
             | ((std::uint64_t) (slot - 1) << 4) | group;
    }
    static std::uint64_t tokenOf (std::uint64_t value) noexcept { return value >> 19; }
    static std::uint8_t groupOf (std::uint64_t value) noexcept { return (std::uint8_t) (value & 15u); }
    static std::uint8_t slotOf (std::uint64_t value) noexcept { return (std::uint8_t) (((value >> 4) & 31u) + 1); }
    static std::uint8_t priorityOf (std::uint64_t value) noexcept { return (std::uint8_t) ((value >> 9) & 127u); }
    static Grid gridOf (std::uint64_t value) noexcept { return (Grid) ((value >> 16) & 7u); }
    static bool isMember (std::uint64_t value, std::uint8_t group) noexcept
    {
        return value != 0 && value != kClaiming && groupOf (value) == group;
    }
    static std::uint64_t generationOf (std::uint64_t value) noexcept { return value & kGenerationMask; }
    static std::int64_t tickOf (std::uint64_t value) noexcept { return (std::int64_t) (value >> 20) + kMinTick; }
    static std::uint64_t encodeClock (std::int64_t tick, std::uint64_t generation) noexcept
    {
        return ((std::uint64_t) (tick - kMinTick) << 20) | generation;
    }
    static bool toTick (double ppq, bool end, std::int64_t& out) noexcept
    {
        const double scaled = end ? std::ceil (ppq * kTicksPerBeat) : std::floor (ppq * kTicksPerBeat);
        if (! std::isfinite (scaled) || scaled <= (double) kMinTick || scaled > (double) kMaxTick)
            return false;
        out = (std::int64_t) scaled;
        return true;
    }
    static bool sameMembership (const Plan& a, const Plan& b) noexcept
    {
        if (a.snapshot.count != b.snapshot.count || a.grid != b.grid || a.beats != b.beats)
            return false;
        for (int i = 0; i < a.snapshot.count; ++i)
        {
            const auto& x = a.snapshot.members[(std::size_t) i];
            const auto& y = b.snapshot.members[(std::size_t) i];
            if (x.token != y.token || x.slot != y.slot || x.priority != y.priority)
                return false;
        }
        return true;
    }
    static void writePlan (GroupState& state, const Plan& value) noexcept
    {
        auto& plane = state.planes[(std::size_t) (value.snapshot.generation % 3)];
        plane.sequence.fetch_add (1, std::memory_order_seq_cst);
        plane.generation.store (value.snapshot.generation, std::memory_order_seq_cst);
        plane.count.store (value.snapshot.count, std::memory_order_seq_cst);
        plane.grid.store ((std::uint8_t) value.grid, std::memory_order_seq_cst);
        plane.beats.store (std::bit_cast<std::uint64_t> (value.beats), std::memory_order_seq_cst);
        plane.activation.store (value.activationTick, std::memory_order_seq_cst);
        for (int i = 0; i < value.snapshot.count; ++i)
        {
            const auto& m = value.snapshot.members[(std::size_t) i];
            plane.rows[(std::size_t) i].store (pack (m.token, value.snapshot.group, m.slot, m.priority, value.grid),
                                               std::memory_order_seq_cst);
        }
        plane.sequence.fetch_add (1, std::memory_order_seq_cst);
    }
    static bool copyPlan (const GroupState& state, std::uint64_t generation, Plan& out) noexcept
    {
        if (generation == 0)
            return false;
        const auto& plane = state.planes[(std::size_t) (generation % 3)];
        const auto sequence = plane.sequence.load (std::memory_order_seq_cst);
        if ((sequence & 1u) != 0 || plane.generation.load (std::memory_order_seq_cst) != generation)
            return false;
        Plan value;
        value.snapshot.generation = generation;
        value.snapshot.count = plane.count.load (std::memory_order_seq_cst);
        value.grid = (Grid) plane.grid.load (std::memory_order_seq_cst);
        value.beats = std::bit_cast<double> (plane.beats.load (std::memory_order_seq_cst));
        value.activationTick = plane.activation.load (std::memory_order_seq_cst);
        if (value.snapshot.count < 1 || value.snapshot.count > kCapacity)
            return false;
        for (int i = 0; i < value.snapshot.count; ++i)
        {
            const auto row = plane.rows[(std::size_t) i].load (std::memory_order_seq_cst);
            value.snapshot.group = groupOf (row);
            value.snapshot.members[(std::size_t) i] = { tokenOf (row), slotOf (row), priorityOf (row) };
        }
        if (plane.sequence.load (std::memory_order_seq_cst) != sequence || ! valid (value.snapshot))
            return false;
        out = value;
        return true;
    }
};

} // namespace gapfill::coop::deterministic
