#include "Coop.h"

#include <algorithm>
#include <cmath>

namespace gapfill::coop
{

namespace { enum DetStatus { DetOff, DetWaiting, DetReady, DetPaused, DetFull, DetTransition, DetPending }; }

deterministic::Scale yieldScale (const CoopView& v, bool havePpq, double ppq) noexcept
{
    if (v.mode != Deterministic || ! havePpq || ! v.deterministicTransport)
        return {};
    const auto decision = deterministic::decisionAt (v.deterministicView, ppq, true, v.yieldPercent);
    return decision.ready ? decision.scale : deterministic::Scale { 0.0f, 0.0f };
}

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

    if (v.mode == Deterministic)
    {
        if (! havePpq)
            return RejectReason::None;
        const auto decision = deterministic::decisionAt (v.deterministicView, ppq, true, v.yieldPercent);
        return ! decision.ready || (! decision.owner && v.yieldPercent >= 100.0f)
            ? RejectReason::CoopYield : RejectReason::None;
    }

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
    releaseDeterministic();
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
    if (deterministicHandle.token != 0)
        releaseDeterministic();
    group = std::clamp (group, 0, (int) NumGroups - 1);

    if (group == GroupOff)
    {
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
    const auto status = detStatus.load (std::memory_order_relaxed);
    if (status != DetOff)
    {
        if (status == DetFull)
            return "Natural fills paused: co-op capacity is 32 instances";
        if (status == DetPaused)
            return "Cooperation paused: no host transport";
        if (status == DetWaiting)
            return "Co-op: waiting for coherent membership / fixed slots";
        if (status == DetPending)
            return "Co-op settings pending; current membership still plays (a peer may not be advancing)";
        if (status == DetTransition)
            return "Co-op membership changes at beat " + juce::String (std::bit_cast<double> (detBoundaryBits.load()), 2);
        return "Deterministic co-op: slot " + juce::String (assignedSlot()) + ", "
             + juce::String (getPeerCount() + 1) + " tracks; owner slot " + juce::String (detOwner.load());
    }
    const auto g = getGroup();

    if (g == GroupOff)
        return {};

    const auto n = getPeerCount();

    if (n <= 0)
        return "Co-op: no other GapFill found";

    return "Co-op " + juce::String::charToString ((juce::juce_wchar) ('A' + g - 1)) + ": "
         + juce::String (n) + (n == 1 ? " other instance" : " other instances");
}

void CoopMember::releaseDeterministic() noexcept
{
    if (deterministicHandle.token != 0)
    {
        deterministicShared->scenes.cancel (deterministicHandle.group);
        deterministicShared->membership.release (deterministicHandle);
    }
    deterministicIdentity.store (0, std::memory_order_release);
    pendingSlot.store (0);
    detAssigned.store (0);
    detStatus.store (DetOff);
    view.deterministicView = {};
    view.deterministicTransport = false;
    if (slotIndex < 0)
    {
        activeGroup.store (0, std::memory_order_relaxed);
        peerCount.store (0, std::memory_order_relaxed);
    }
}

void CoopMember::configureDeterministic (const DeterministicConfig& settings) noexcept
{
    const int group = std::clamp (settings.group, 0, 8);
    if (group == 0)
    {
        release();
        return;
    }
    if (slotIndex >= 0)
        release();                                // leave the legacy registry before joining the new one
    auto& registry = deterministicShared->membership;
    const auto requested = std::clamp (settings.slot, 1, 32);
    const auto priority = (std::uint8_t) std::clamp (settings.priority, 0, 100);
    const auto grid = (deterministic::Grid) std::clamp ((int) settings.grid, 0, 4);
    int slot = requested;
    const bool repair = deterministicHandle.token == 0 || deterministicHandle.group != group
                     || requested != lastRequestedSlot.load();
    const auto pending = pendingSlot.load (std::memory_order_acquire);
    if (pending > 0 && requested == lastRequestedSlot.load())
        slot = pending;                           // preserve a reserved auto-slot until host state catches up
    else if (pending > 0)
        pendingSlot.store (0);                    // host acknowledged it, or an explicit newer user edit wins
    lastRequestedSlot.store (requested);
    if (deterministicHandle.token == 0)
        deterministicHandle = registry.claim ((std::uint8_t) group, (std::uint8_t) slot, priority, grid);
    else
        registry.update (deterministicHandle, (std::uint8_t) group, (std::uint8_t) slot, priority, grid);
    activeGroup.store (group, std::memory_order_relaxed);
    if (deterministicHandle.token == 0)
    {
        detStatus.store (DetFull);
        return;
    }
    const auto fixed = repair ? registry.repairSlot ((std::uint8_t) group, deterministicHandle.token) : 0;
    if (fixed != 0)
    {
        slot = fixed;
        pendingSlot.store (slot, std::memory_order_release);
    }
    detAssigned.store (slot);
    deterministicIdentity.store ((deterministicHandle.token << 4) | (std::uint64_t) group, std::memory_order_release);
    detStatus.store (DetWaiting);
}

CoopSettings CoopMember::beginDeterministicChunk (const DeterministicConfig& settings,
                                                const DeterministicTiming& timing) noexcept
{
    configureDeterministic (settings);
    if (deterministicHandle.token == 0)
    {
        if (settings.group <= 0 || ! timing.valid)
            return {};
        view.mode = Deterministic;
        view.deterministicTransport = true;
        view.deterministicView = {};              // fail closed, never invent a second owner on capacity failure
        CoopSettings result;
        result.view = &view;
        return result;
    }
    auto& registry = deterministicShared->membership;
    if (registry.maintain (deterministicHandle.group, timing.beatsPerBar))
        deterministicShared->scenes.cancel (deterministicHandle.group);
    view.mode = Deterministic;
    view.yieldPercent = std::isfinite (settings.yieldPercent) ? std::clamp (settings.yieldPercent, 0.0f, 100.0f) : 70.0f;
    view.deterministicTransport = timing.valid && std::isfinite (timing.ppq)
                                  && std::isfinite (timing.samplesPerBeat) && timing.samplesPerBeat > 1.0;
    if (! view.deterministicTransport)
    {
        detStatus.store (DetPaused);
        return {};
    }
    const auto end = timing.ppq + (double) std::max (0, timing.numSamples) / timing.samplesPerBeat;
    if (! registry.admit (deterministicHandle, timing.ppq, end, view.deterministicView))
    {
        view.deterministicView = {};
        detStatus.store (DetWaiting);
    }
    else
    {
        const auto decision = deterministic::decisionAt (view.deterministicView, timing.ppq, true, view.yieldPercent);
        const auto boundary = (double) view.deterministicView.current.activationTick / deterministic::kTicksPerBeat;
        detBoundaryBits.store (std::bit_cast<std::uint64_t> (boundary));
        detStatus.store (! decision.ready ? DetWaiting
            : registry.pending (deterministicHandle.group) ? DetPending
            : ! view.deterministicView.forceCurrent && timing.ppq + deterministic::kPpqRoundingBeats < boundary ? DetTransition : DetReady);
        detOwner.store (decision.ownerSlot);
        const auto& selected = view.deterministicView.forceCurrent || (timing.ppq + deterministic::kPpqRoundingBeats) * deterministic::kTicksPerBeat
                              >= (double) view.deterministicView.current.activationTick
                             ? view.deterministicView.current : view.deterministicView.previous;
        peerCount.store (std::max (0, selected.snapshot.count - 1));
    }
    CoopSettings result;
    result.view = &view;
    return result;
}

void CoopMember::noteTransportJump() noexcept
{
    deterministicShared->membership.noteTransportJump (deterministicHandle);
    cancelGroupScenes();
}

int CoopMember::repairDuplicateSlot() noexcept
{
    const auto identity = deterministicIdentity.load (std::memory_order_acquire);
    const auto slot = deterministicShared->membership.repairSlot ((std::uint8_t) (identity & 15u), identity >> 4);
    if (slot != 0)
        pendingSlot.store (slot, std::memory_order_release);
    return pendingSlot.load (std::memory_order_acquire);
}

void CoopMember::applyAssignedSlotToState (juce::ValueTree& tree) const
{
    if (deterministicIdentity.load (std::memory_order_acquire) == 0)
        return;
    auto node = tree.getChildWithProperty ("id", "coopSlot");
    const int assigned = assignedSlot();
    if (node.isValid() && assigned >= 1 && assigned <= 32
        && ((int) node.getProperty ("value") == lastRequestedSlot.load()
            || (int) node.getProperty ("value") == assigned))
        node.setProperty ("value", assigned, nullptr);
}

void CoopMember::cancelGroupScenes() noexcept
{
    const auto identity = deterministicIdentity.load (std::memory_order_acquire);
    if (identity != 0)
        deterministicShared->scenes.cancel ((int) (identity & 15u));
}

std::uint64_t CoopMember::groupCancelGeneration (int group) const noexcept
{
    return deterministicShared->scenes.cancelGeneration (group);
}

bool CoopMember::requestGroupScene (int index) noexcept
{
    const auto identity = deterministicIdentity.load (std::memory_order_acquire);
    deterministic::Plan plan;
    double boundary = 0.0;
    return identity != 0 && deterministicShared->membership.sceneDeadline ((std::uint8_t) (identity & 15u), boundary, plan)
        && requestGroupScene (index, boundary);
}

bool CoopMember::requestGroupScene (int index, double boundary) noexcept
{
    const auto identity = deterministicIdentity.load (std::memory_order_acquire);
    const int group = (int) (identity & 15u);
    deterministic::Plan plan;
    double earliest = 0.0;
    if (identity == 0 || detStatus.load() == DetPaused
        || ! deterministicShared->membership.sceneDeadline ((std::uint8_t) group, earliest, plan) || boundary < earliest)
        return false;
    bool member = false;
    for (int i = 0; i < plan.snapshot.count; ++i)
        member = member || plan.snapshot.members[(std::size_t) i].token == identity >> 4;
    return member && deterministicShared->scenes.request (group, index, boundary, plan.snapshot.generation);
}

bool CoopMember::pollGroupSceneEvent (GroupSceneEvent& event) noexcept
{
    const auto identity = deterministicIdentity.load (std::memory_order_acquire);
    const int group = (int) (identity & 15u);
    if (sceneCursorGroup != group)
    {
        const int previous = sceneCursorGroup;
        sceneCursorGroup = group;
        sceneCursor = {};
        if (previous != 0)
        {
            event = {}; event.kind = GroupSceneEvent::Cancel; event.group = previous;
            return true;
        }
    }
    auto cursor = sceneCursor;
    if (group == 0 || ! deterministicShared->scenes.poll (group, cursor, event))
        return false;
    if (event.kind == GroupSceneEvent::Recall)
    {
        deterministic::Plan plan;
        if (! deterministicShared->membership.latest ((std::uint8_t) group, plan))
            return false;                         // keep the old cursor; retry the same request next message tick
        bool member = false;
        for (int i = 0; i < plan.snapshot.count; ++i)
            member = member || plan.snapshot.members[(std::size_t) i].token == identity >> 4;
        if (! member || event.membershipGeneration != plan.snapshot.generation)
            event.kind = GroupSceneEvent::Cancel;
    }
    sceneCursor = cursor;
    return true;
}

} // namespace gapfill::coop
