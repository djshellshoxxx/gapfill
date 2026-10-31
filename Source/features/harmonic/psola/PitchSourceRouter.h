#pragma once
#include "PitchTracker.h"
#include <array>

namespace gapfill::harmonic::psola
{
/** Audio-only coordinates supplied after CaptureLatch::update(). Generation is
    nonzero and never reused. Keep the last started ticket visible on completion,
    including a copy which starts and finishes during one callback. */
struct PitchCopyTicket
{
    std::uint64_t generation = 0;
    int targetIndex = -1;
    std::int64_t liveStart = 0;
    int total = 0;
};
struct PitchSourceChunk
{
    // Actual analysis writer: the live tap, or normal history if a detailed
    // Capture mode does not record its tap while released.
    std::int64_t liveFirst = 0;
    // First written history sample; when held, its frozen NEXT-write position.
    std::int64_t historyFirst = 0;
    bool historyWritten = true;
    bool held = false;
    PitchCopyTicket copy;
    int activeSnapshotIndex = -1;
    std::uint64_t activeSnapshotGeneration = 0;
};

/** One incremental detector plus immutable source-coordinate frame banks.
    Freeze occurs before current held input; pending snapshots freeze at copy
    START, then activate only with a matching index and generation. No PCM
    catch-up, borrowed peer pointers, allocations, or cross-thread reads. */
class PitchSourceRouter
{
public:
    using Chunk = PitchSourceChunk;
    using CopyTicket = PitchCopyTicket;
    using Query = PitchTracker::Query;
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setActive (bool) noexcept;
    bool isActive() const noexcept { return tracker.isActive(); }
    void push (const float* mono, int count, const Chunk&) noexcept;
    Query query (std::int64_t fromInclusive, std::int64_t toInclusive) const noexcept;
    const PitchTracker& detector() const noexcept { return tracker; }

private:
    struct FrameBank
    {
        std::array<PitchFrame, kPitchFrames> frames {};
        int count = 0;
        std::int64_t first = 0, last = -1;
        std::uint64_t generation = 0;
        Query query (std::int64_t from, std::int64_t to) const noexcept;
    };
    void freeze (FrameBank&, std::int64_t analysisFirst, std::int64_t sourceFirst,
                 std::int64_t sourceLast, std::uint64_t generation) const noexcept;
    PitchTracker tracker;
    FrameBank heldHistory;
    std::array<FrameBank, 2> snapshots;
    bool wasHeld = false, mapping = false;
    std::int64_t liveSegmentFirst = 0, historySegmentFirst = 0, expectedLive = 0, expectedHistory = 0;
    int selectedSnapshot = -1;
    std::uint64_t selectedGeneration = 0;
};
static_assert (sizeof (PitchSourceRouter) <= 40 * 1024);
} // namespace gapfill::harmonic::psola
