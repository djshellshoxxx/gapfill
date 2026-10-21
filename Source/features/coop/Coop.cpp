#include "Coop.h"

#include <algorithm>
#include <cmath>

namespace gapfill::coop
{

double timeAt (const CoopView& v, bool havePpq, double ppqAtChunkStart, std::int64_t offsetSamples,
               double samplesPerBeat) noexcept
{
    if (havePpq && samplesPerBeat > 1.0)
        return ppqAtChunkStart + (double) offsetSamples / samplesPerBeat;

    return v.wallAtChunkStart + 1000.0 * (double) offsetSamples / v.sampleRate;
}

RejectReason evaluate (const CoopView& v, bool havePpq, double ppq, std::int64_t decisionOffsetSamples,
                       double samplesPerBeat, double beatsPerBar) noexcept
{
    if (! v.active())
        return RejectReason::None;

    const auto domain = (std::uint32_t) (havePpq && samplesPerBeat > 1.0 ? DomainPpq : DomainWall);
    const auto now = domain == DomainPpq ? ppq : v.wallAtChunkStart + 1000.0 * (double) decisionOffsetSamples / v.sampleRate;

    // Only members that publish in my time domain can be compared with me.
    int comparable = 0;
    bool anyActive = false, followWindow = false;
    int lowerIds = 0;

    // 50 ms in domain units
    const double window = domain == DomainPpq ? (kFollowWindowMs * 0.001 * v.sampleRate) / samplesPerBeat : kFollowWindowMs;

    for (int i = 0; i < v.numPeers; ++i)
    {
        const auto& p = v.peers[i];

        if (p.domain != domain)
            continue;

        ++comparable;
        anyActive = anyActive || (p.lastFillStart <= now && now < p.activeUntil);     // a stale future value after a loop-back never yields
        followWindow = followWindow || (p.lastFillStart <= now && now - p.lastFillStart <= window);
        lowerIds += p.id < v.selfId ? 1 : 0;
    }

    if (comparable == 0)
        return RejectReason::None;

    switch (v.mode)
    {
        case AvoidOverlap:
            return anyActive ? RejectReason::CoopYield : RejectReason::None;

        case Alternate:
        {
            if (domain != DomainPpq || beatsPerBar <= 0.0)
                return RejectReason::None;                    // paused without a host position

            const auto bar = (long long) std::floor (ppq / beatsPerBar);
            const auto members = (long long) comparable + 1;
            const auto owner = ((bar % members) + members) % members;
            return owner == (long long) lowerIds ? RejectReason::None : RejectReason::CoopYield;
        }

        case Follow:
            return followWindow ? RejectReason::None : RejectReason::CoopYield;

        default:
            return RejectReason::None;
    }
}

//==============================================================================
CoopMember::CoopMember() = default;

CoopMember::~CoopMember()
{
    release();
}

bool CoopMember::claim (int group) noexcept
{
    auto& reg = *shared;
    const auto id = reg.nextId.fetch_add (1, std::memory_order_relaxed);

    for (int i = 0; i < kMaxSlots; ++i)
    {
        std::uint32_t expected = 0;

        if (reg.slots[i].id.compare_exchange_strong (expected, id, std::memory_order_acq_rel))
        {
            auto& s = reg.slots[i];
            s.group.store ((std::uint32_t) group, std::memory_order_release);
            s.activeUntil.store (-1.0e18, std::memory_order_relaxed);
            s.lastFillStart.store (-1.0e18, std::memory_order_relaxed);
            slotIndex = i;
            myId.store (id, std::memory_order_relaxed);
            return true;
        }
    }

    return false;                    // table full: behave as if alone
}

void CoopMember::release() noexcept
{
    if (slotIndex >= 0)
    {
        auto& s = shared->slots[slotIndex];
        s.group.store (0, std::memory_order_release);
        s.id.store (0, std::memory_order_release);
    }

    slotIndex = -1;
    myId.store (0, std::memory_order_relaxed);
    activeGroup.store (0, std::memory_order_relaxed);
    peerCount.store (0, std::memory_order_relaxed);
    view = CoopView();
    fillsActive = 0;
}

CoopSettings CoopMember::beginChunk (int group, int mode, double sampleRate, double nowWallMs,
                                     std::uint32_t nowMs) noexcept
{
    group = std::clamp (group, 0, (int) NumGroups - 1);

    if (group == GroupOff)
    {
        if (slotIndex >= 0)
            release();

        return {};
    }

    if (slotIndex < 0 && ! claim (group))
        return {};

    auto& reg = *shared;
    auto& mine = reg.slots[slotIndex];
    mine.group.store ((std::uint32_t) group, std::memory_order_release);
    mine.lastSeenMs.store (nowMs, std::memory_order_relaxed);
    activeGroup.store (group, std::memory_order_relaxed);

    view.mode = std::clamp (mode, 0, (int) NumModes - 1);
    view.selfId = myId.load (std::memory_order_relaxed);
    view.sampleRate = sampleRate;
    view.wallAtChunkStart = nowWallMs;
    view.numPeers = 0;

    for (int i = 0; i < kMaxSlots; ++i)
    {
        if (i == slotIndex)
            continue;

        const auto& s = reg.slots[i];
        const auto id = s.id.load (std::memory_order_acquire);

        if (id == 0 || s.group.load (std::memory_order_acquire) != (std::uint32_t) group)
            continue;

        if (nowMs - s.lastSeenMs.load (std::memory_order_relaxed) > kStaleMs)
            continue;                                        // not refreshed for 2 s: ignored

        auto& p = view.peers[view.numPeers++];
        p.id = id;
        p.domain = s.domain.load (std::memory_order_relaxed);
        p.activeUntil = s.activeUntil.load (std::memory_order_relaxed);
        p.lastFillStart = s.lastFillStart.load (std::memory_order_relaxed);
    }

    peerCount.store (view.numPeers, std::memory_order_relaxed);

    CoopSettings out;
    out.view = view.numPeers > 0 ? &view : nullptr;
    return out;
}

void CoopMember::publish (const TransitionLog& log, std::int64_t chunkOutputStart, int chunkSamples,
                          bool havePpq, double ppqAtChunkStart, double samplesPerBeat) noexcept
{
    if (slotIndex < 0)
        return;

    auto& mine = shared->slots[slotIndex];
    const bool ppqDomain = havePpq && samplesPerBeat > 1.0;
    mine.domain.store (ppqDomain ? DomainPpq : DomainWall, std::memory_order_relaxed);

    auto toTime = [&] (std::int64_t pos)
    {
        return timeAt (view, ppqDomain, ppqAtChunkStart, pos - chunkOutputStart, samplesPerBeat);
    };

    const auto chunkEnd = chunkOutputStart + chunkSamples;

    for (int i = 0; i < log.count; ++i)
    {
        const auto& t = log.items[i];

        if (t.pos < chunkOutputStart || t.pos >= chunkEnd)
            continue;

        if (t.type == Transition::FillStart)
        {
            const auto start = toTime (t.pos);
            const auto length = ppqDomain ? (double) t.lengthSamples / samplesPerBeat
                                          : 1000.0 * (double) t.lengthSamples / view.sampleRate;
            mine.lastFillStart.store (start, std::memory_order_relaxed);
            // A second overlapping fill extends the busy interval; the first fill after silence restarts it.
            mine.activeUntil.store (fillsActive > 0 ? std::max (mine.activeUntil.load (std::memory_order_relaxed), start + length)
                                                    : start + length,
                                    std::memory_order_relaxed);
            ++fillsActive;
        }
        else if (t.type == Transition::FillEnd && fillsActive > 0)
        {
            if (--fillsActive == 0)
                mine.activeUntil.store (toTime (t.pos), std::memory_order_relaxed);   // ended early (protection fade)
        }
    }
}

juce::String CoopMember::describe() const
{
    const auto g = getGroup();

    if (g == GroupOff)
        return {};

    const auto n = getPeerCount();

    if (n <= 0)
        return "Co-op: no other GapFill found";

    return "Co-op " + juce::String::charToString ((juce::juce_wchar) ('A' + g - 1)) + ": "
         + juce::String (n) + (n == 1 ? " other instance" : " other instances");
}

} // namespace gapfill::coop
