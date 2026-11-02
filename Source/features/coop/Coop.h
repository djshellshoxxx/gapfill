#pragma once

// F-X06 Multi-instance cooperation, safe subset (docs/dev/12 F-X06, docs/dev/features/F-X06).
//
// Parameters: coopGroup (Off, A-D), coopMode (Avoid overlap, Alternate, Follow).
//
// What is implemented
//  * CoopRegistry: process-wide table of 32 slots of atomics (instance id, group,
//    time domain, activeUntil, lastFillStart, lastSeenMs), shared through a
//    juce::SharedResourcePointer. PLAIN DATA ONLY: no pointers to processors, no
//    locks, no allocation, no network, no files; trivially destructible slots.
//    A slot not refreshed for 2 s is ignored; it is freed on group Off,
//    releaseResources and in the destructor. Claim/release are wait-free loops over
//    32 slots, so they are safe on the audio thread.
//  * Avoid overlap : skip a fill (CoopYield) while another member of the group has a
//                    fill active.
//  * Alternate     : members take turns bar by bar, in instance-id order. DETERMINISTIC
//                    (a pure function of the bar index and the member list); paused
//                    without a valid host position.
//  * Follow        : may start a fill only within 50 ms after another member started one.
//  * Reads use the values published in the previous block.
//  * A lone member (or an instance whose peers are in another process) is inert.
//
// Honest limits (also in Help / docs/progress/WP10.md)
//  * Hosts that sandbox plug-ins per process cannot see each other; the status says
//    "no other GapFill found" and the instance behaves exactly as with Off.
//  * Avoid overlap and Follow depend on the order and timing in which the host runs
//    the instances, so their results are NOT bit-reproducible. Group Off (default)
//    keeps everything deterministic. Alternate is reproducible for a fixed member set.
//  * Time domain: musical position (ppq) when the host reports one, otherwise the wall
//    clock. Only slots of the same domain are compared.
//  * Deterministic mode adds per-slot priority/yield, fixed-slot rendezvous
//    ownership and group-wide scene recall. Host transport supplies its timing.

#include "CoopFwd.h"
#include "DetRegistry.h"
#include "GroupScenes.h"
#include "../../dsp/EngineTransitions.h"
#include "../../dsp/SchedulerTypes.h"

#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <cstdint>

namespace gapfill::coop
{

enum Group { GroupOff = 0, GroupA, GroupB, GroupC, GroupD, GroupE, GroupF, GroupG, GroupH, NumGroups };
// Deterministic is INTERNAL. Released coopMode stays a three-choice parameter;
// COOPEXT's toggle selects this fourth engine path without renormalizing it.
enum Mode  { AvoidOverlap = 0, Alternate, Follow, NumModes, Deterministic = NumModes };

inline constexpr int kMaxSlots = 32;
inline constexpr std::uint32_t kStaleMs = 2000;
inline constexpr double kFollowWindowMs = 50.0;

enum Domain : std::uint8_t { DomainWall = 0, DomainPpq = 1 };

struct Slot
{
    std::atomic<std::uint32_t> id { 0 };            ///< 0 = free
    std::atomic<std::uint32_t> group { 0 };
    std::atomic<std::uint32_t> domain { DomainWall };
    std::atomic<std::uint32_t> lastSeenMs { 0 };
    std::atomic<double> activeUntil { -1.0e18 };
    std::atomic<double> lastFillStart { -1.0e18 };
};

static_assert (std::atomic<double>::is_always_lock_free, "the co-op registry must stay lock-free");
static_assert (std::atomic<std::uint32_t>::is_always_lock_free, "the co-op registry must stay lock-free");

struct CoopRegistry
{
    Slot slots[kMaxSlots];
    std::atomic<std::uint32_t> nextId { 1 };
};

/** What the engine may know about the other members of the group at a decision. */
struct CoopView
{
    struct Peer
    {
        std::uint32_t id = 0;
        std::uint32_t domain = DomainWall;
        double activeUntil = -1.0e18;
        double lastFillStart = -1.0e18;
    };

    int mode = AvoidOverlap;
    std::uint32_t selfId = 0;
    int numPeers = 0;                      ///< other live members of my group
    Peer peers[kMaxSlots];
    double wallAtChunkStart = 0.0;         ///< ms, for the wall-clock domain
    double sampleRate = 48000.0;
    deterministic::View deterministicView;
    float yieldPercent = 70.0f;
    bool deterministicTransport = false;
    bool active() const noexcept   { return mode == Deterministic ? deterministicTransport : selfId != 0 && numPeers > 0; }
};

struct DeterministicConfig
{
    int group = 0, slot = 1, priority = 50;
    float yieldPercent = 70.0f;
    deterministic::Grid grid = deterministic::Grid::Bar;
};

struct DeterministicTiming
{
    bool valid = false;
    double ppq = 0.0, samplesPerBeat = 0.0, beatsPerBar = 4.0;
    int numSamples = 0;
};

/** Apply before the natural-fill probability roll, after computing decision PPQ.
    A pending membership has zero scales; unavailable transport stays neutral. */
deterministic::Scale yieldScale (const CoopView&, bool havePpq, double decisionPpq) noexcept;

using GroupSceneEvent = deterministic::GroupSceneEvent;

struct DeterministicServices
{
    deterministic::Registry membership;
    deterministic::GroupScenes scenes;
};

/** The decision: CoopYield or None. `havePpq` selects the time domain; `decisionOffsetSamples` is
    the decision sample minus the first sample of the current chunk. Pure and allocation-free. */
RejectReason evaluate (const CoopView&, bool havePpq, double ppq, std::int64_t decisionOffsetSamples,
                       double samplesPerBeat, double beatsPerBar) noexcept;

/** Time of the decision in the view's domain (exposed for publishing and tests). */
double timeAt (const CoopView&, bool havePpq, double ppqAtChunkStart, std::int64_t offsetSamples,
               double samplesPerBeat) noexcept;

//==============================================================================
/** One processor's membership. The message thread owns construction, destruction
    and presentation/state helpers; preparation/audio methods are marked below. */
class CoopMember
{
public:
    CoopMember();
    ~CoopMember();

    CoopMember (const CoopMember&) = delete;
    CoopMember& operator= (const CoopMember&) = delete;

    /** Frees the slot (group Off, releaseResources, destructor). Wait-free. */
    void release() noexcept;

    /** Once per chunk. Claims a slot when `group` != Off, refreshes lastSeen, snapshots the peers
        published up to the previous block. Returns the settings for the engine (view null = inert). */
    CoopSettings beginChunk (int group, int mode, double sampleRate, double nowWallMs,
                             std::uint32_t nowMs) noexcept;

    /** Preparation/audio owner: register every enabled instance during prepare,
        BEFORE any instance processes. All old configurations stay off. */
    void configureDeterministic (const DeterministicConfig&) noexcept;
    CoopSettings beginDeterministicChunk (const DeterministicConfig&, const DeterministicTiming&) noexcept;
    void noteTransportJump() noexcept;

    /** Message thread; returned slot must be synchronized to coopSlot through
        the normal host notification path, including when the editor is closed. */
    int repairDuplicateSlot() noexcept;
    int pendingSlotForHost() const noexcept { return pendingSlot.load (std::memory_order_relaxed); }
    int pendingSlotForHost (int currentRequested) const noexcept
    {
        return currentRequested == lastRequestedSlot.load() ? pendingSlotForHost() : 0;
    }
    int assignedSlot() const noexcept { const int p = pendingSlotForHost(); return p > 0 ? p : detAssigned.load(); }
    /** Host/preset serializer edits its PRIVATE copy, never the live APVTS. */
    void applyAssignedSlotToState (juce::ValueTree&) const;

    bool requestGroupScene (int sceneIndex) noexcept;
    bool requestGroupScene (int sceneIndex, double absolutePpq) noexcept;
    bool pollGroupSceneEvent (GroupSceneEvent&) noexcept;           // message thread
    void cancelGroupScenes() noexcept;                            // any thread, atomics only
    std::uint64_t groupCancelGeneration (int group) const noexcept;

    /** After the engine ran: publishes this chunk's fill starts / ends. `chunkOutputStart` is the
        output position of the chunk's first sample. */
    void publish (const TransitionLog&, std::int64_t chunkOutputStart, int chunkSamples, bool havePpq,
                  double ppqAtChunkStart, double samplesPerBeat) noexcept;

    /** For the footer: 0 = Off, else number of other live members (can be 0 = "no other GapFill found"). */
    int  getPeerCount() const noexcept    { return peerCount.load (std::memory_order_relaxed); }
    int  getGroup() const noexcept        { return activeGroup.load (std::memory_order_relaxed); }
    std::uint32_t getId() const noexcept  { return myId.load (std::memory_order_relaxed); }

    /** MESSAGE THREAD ONLY. Localized presentation; no membership/state edits.
        "Co-op A: 2 other instances" / deterministic status; empty when Off. */
    juce::String describe() const;

private:
    bool claim (int group) noexcept;
    void releaseDeterministic() noexcept;

    juce::SharedResourcePointer<CoopRegistry> shared;
    int slotIndex = -1;
    std::atomic<std::uint32_t> myId { 0 };
    std::atomic<int> activeGroup { 0 };
    std::atomic<int> peerCount { 0 };
    CoopView view;
    int fillsActive = 0;

    juce::SharedResourcePointer<DeterministicServices> deterministicShared;
    deterministic::Handle deterministicHandle;                    // preparation/audio owner only
    std::atomic<std::uint64_t> deterministicIdentity { 0 };        // token << 4 | group
    std::atomic<std::uint64_t> detBoundaryBits { 0 };
    std::atomic<int> pendingSlot { 0 }, detAssigned { 0 }, detStatus { 0 }, detOwner { 0 };
    std::atomic<int> lastRequestedSlot { 1 };
    int sceneCursorGroup = 0;                                     // message thread only
    deterministic::SceneCursor sceneCursor;
};

} // namespace gapfill::coop
