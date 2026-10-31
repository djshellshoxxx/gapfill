#pragma once

// Bounded latest-request channel. The receiver resolves its OWN scene bank;
// no scene data, pointers to processors or file work crosses instance boundaries.
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace gapfill::coop::deterministic
{

struct GroupSceneEvent
{
    enum Kind { Recall, Cancel } kind = Recall;
    int group = 0, sceneIndex = 0;
    double targetPpq = 0.0;
    std::uint64_t generation = 0, cancelGeneration = 0, membershipGeneration = 0;
};

struct SceneCursor
{
    std::uint64_t request = 0, cancel = 0;
    bool exhausted = false;
};

class GroupScenes
{
public:
    std::uint64_t cancelGeneration (int group) const noexcept
    {
        return group >= 1 && group <= 8 ? groups[(std::size_t) group - 1].cancel.load() : 0;
    }

    bool request (int group, int index, double targetPpq, std::uint64_t membershipGeneration) noexcept
    {
        if (group < 1 || group > 8 || index < 0 || index >= 64 || ! std::isfinite (targetPpq)
            || std::abs (targetPpq) > 0x1p23 || membershipGeneration == 0)
            return false;
        auto& cell = groups[(std::size_t) group - 1];
        auto sequence = cell.sequence.load();
        if ((sequence & 1u) != 0 || sequence >= std::numeric_limits<std::uint64_t>::max() - 2
            || cell.exhausted.load() || ! cell.sequence.compare_exchange_strong (sequence, sequence + 1))
            return false;                         // bounded try, no audio-thread waiting
        cell.index.store (index);
        cell.target.store (std::bit_cast<std::uint64_t> (targetPpq));
        cell.membership.store (membershipGeneration);
        cell.requestEpoch.store (cell.cancel.load());
        cell.sequence.store (sequence + 2);
        return true;
    }

    void cancel (int group) noexcept
    {
        if (group >= 1 && group <= 8)
        {
            auto& cell = groups[(std::size_t) group - 1];
            if (cell.cancel.fetch_add (1) == std::numeric_limits<std::uint64_t>::max())
                cell.exhausted.store (true);       // future requests remain disabled after counter exhaustion
        }
    }

    bool poll (int group, SceneCursor& cursor, GroupSceneEvent& out) const noexcept
    {
        if (group < 1 || group > 8)
            return false;
        const auto& cell = groups[(std::size_t) group - 1];
        if (cell.exhausted.load())
        {
            if (cursor.exhausted)
                return false;
            cursor.exhausted = true;
            out = {}; out.kind = GroupSceneEvent::Cancel; out.group = group;
            return true;
        }
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto sequence = cell.sequence.load();
            if ((sequence & 1u) != 0)
                continue;
            GroupSceneEvent event;
            event.group = group;
            event.generation = sequence / 2;
            event.sceneIndex = cell.index.load();
            event.targetPpq = std::bit_cast<double> (cell.target.load());
            event.membershipGeneration = cell.membership.load();
            const auto requestEpoch = cell.requestEpoch.load();
            const auto cancellation = cell.cancel.load();
            event.cancelGeneration = cancellation;
            if (cell.sequence.load() != sequence)
                continue;
            if (cursor.cancel != cancellation)
            {
                cursor.cancel = cancellation;
                // Keep a request that was explicitly issued AFTER this cancel.
                if (requestEpoch != cancellation)
                    cursor.request = sequence;
                event.kind = GroupSceneEvent::Cancel;
                out = event;
                return true;
            }
            if (sequence == 0 || cursor.request == sequence)
                return false;
            cursor.request = sequence;
            event.kind = requestEpoch == cancellation ? GroupSceneEvent::Recall : GroupSceneEvent::Cancel;
            out = event;
            return true;
        }
        return false;
    }

private:
    struct Cell
    {
        // Sequential consistency gives a single order across the sequence and
        // payload atoms, including weak-memory targets and snapshot recycling.
        std::atomic<std::uint64_t> sequence { 0 }, cancel { 0 }, requestEpoch { 0 }, target { 0 }, membership { 0 };
        std::atomic<int> index { 0 };
        std::atomic<bool> exhausted { false };
    };
    std::array<Cell, 8> groups;
};

} // namespace gapfill::coop::deterministic
