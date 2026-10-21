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
//  * Not implemented (specified for later): per-slot priority/yield, rendezvous-hash
//    ownership with a fixed slot number, group-wide scene recall, shared tempo estimate.

#include "CoopFwd.h"
#include "../../dsp/EngineTransitions.h"
#include "../../dsp/SchedulerTypes.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdint>

namespace gapfill::coop
{

enum Group { GroupOff = 0, GroupA, GroupB, GroupC, GroupD, NumGroups };
enum Mode  { AvoidOverlap = 0, Alternate, Follow, NumModes };

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
    bool active() const noexcept   { return selfId != 0 && numPeers > 0; }
};

/** The decision: CoopYield or None. `havePpq` selects the time domain; `decisionOffsetSamples` is
    the decision sample minus the first sample of the current chunk. Pure and allocation-free. */
RejectReason evaluate (const CoopView&, bool havePpq, double ppq, std::int64_t decisionOffsetSamples,
                       double samplesPerBeat, double beatsPerBar) noexcept;

/** Time of the decision in the view's domain (exposed for publishing and tests). */
double timeAt (const CoopView&, bool havePpq, double ppqAtChunkStart, std::int64_t offsetSamples,
               double samplesPerBeat) noexcept;

//==============================================================================
/** One processor's membership. Message thread constructs/destroys; the audio thread calls the rest. */
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

    /** After the engine ran: publishes this chunk's fill starts / ends. `chunkOutputStart` is the
        output position of the chunk's first sample. */
    void publish (const TransitionLog&, std::int64_t chunkOutputStart, int chunkSamples, bool havePpq,
                  double ppqAtChunkStart, double samplesPerBeat) noexcept;

    /** For the footer: 0 = Off, else number of other live members (can be 0 = "no other GapFill found"). */
    int  getPeerCount() const noexcept    { return peerCount.load (std::memory_order_relaxed); }
    int  getGroup() const noexcept        { return activeGroup.load (std::memory_order_relaxed); }
    std::uint32_t getId() const noexcept  { return myId.load (std::memory_order_relaxed); }

    /** "Co-op A: 2 other instances" / "Co-op: no other GapFill found"; empty when Off. */
    juce::String describe() const;

private:
    bool claim (int group) noexcept;

    juce::SharedResourcePointer<CoopRegistry> shared;
    int slotIndex = -1;
    std::atomic<std::uint32_t> myId { 0 };
    std::atomic<int> activeGroup { 0 };
    std::atomic<int> peerCount { 0 };
    CoopView view;
    int fillsActive = 0;
};

} // namespace gapfill::coop
