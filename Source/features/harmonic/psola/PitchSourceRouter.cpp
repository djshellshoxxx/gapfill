#include "PitchSourceRouter.h"
#include <algorithm>
#include <limits>

namespace gapfill::harmonic::psola
{
void PitchSourceRouter::prepare (double rate) noexcept
{
    tracker.prepare (rate);
    reset();
}
void PitchSourceRouter::reset() noexcept
{
    tracker.reset();
    heldHistory = {}; snapshots = {};
    wasHeld = mapping = false;
    liveSegmentFirst = historySegmentFirst = expectedLive = expectedHistory = 0;
    selectedSnapshot = -1; selectedGeneration = 0;
}
void PitchSourceRouter::setActive (bool active) noexcept
{
    if (tracker.isActive() != active) { tracker.setActive (active); reset(); }
}

void PitchSourceRouter::freeze (FrameBank& bank, std::int64_t analysisFirst,
                                std::int64_t sourceFirst, std::int64_t sourceLast,
                                std::uint64_t generation) const noexcept
{
    bank = {}; bank.first = sourceFirst; bank.last = sourceLast; bank.generation = generation;
    if (analysisFirst < 0 || sourceFirst < 0 || sourceLast < sourceFirst) return;
    for (int i = 0; i < tracker.storedFrames(); ++i)
    {
        const auto value = tracker.frame (i);
        if (value.histPos < analysisFirst || value.histPos - analysisFirst < tracker.sourceFootprintSamples()) continue;
        const auto distance = value.histPos - analysisFirst;
        if (distance > sourceLast - sourceFirst) continue;
        auto& destination = bank.frames[(std::size_t) bank.count++];
        destination = value;
        destination.histPos = sourceFirst + distance;
    }
}

void PitchSourceRouter::push (const float* mono, int count, const Chunk& c) noexcept
{
    if (! isActive() || mono == nullptr || count <= 0) return;
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (c.liveFirst < 0 || c.historyFirst < 0 || c.liveFirst > maximum - count
        || (c.historyWritten && c.historyFirst > maximum - count)) { reset(); return; }

    // The history sound freezes BEFORE this callback's incoming held audio.
    if (c.held && ! wasHeld)
    {
        if (mapping && c.historyFirst == expectedHistory)
            freeze (heldHistory, liveSegmentFirst, historySegmentFirst, c.historyFirst - 1, 0);
        else heldHistory = {};
    }

    if (c.historyWritten && ! c.held)
    {
        if (! mapping || wasHeld || c.liveFirst != expectedLive || c.historyFirst != expectedHistory)
        {
            liveSegmentFirst = c.liveFirst;
            historySegmentFirst = c.historyFirst;
        }
        mapping = true;
        expectedLive = c.liveFirst + count;
        expectedHistory = c.historyFirst + count;
    }
    else mapping = false;

    tracker.push (mono, count, c.liveFirst);
    const auto& ticket = c.copy;
    if (c.held && ticket.generation != 0 && ticket.targetIndex >= 0 && ticket.targetIndex < 2
        && ticket.liveStart >= 0 && ticket.total > 0 && ticket.liveStart <= maximum - ticket.total)
    {
        auto& bank = snapshots[(std::size_t) ticket.targetIndex];
        if (bank.generation != ticket.generation)
            freeze (bank, ticket.liveStart, 0, ticket.total - 1, ticket.generation);
    }
    wasHeld = c.held;
    selectedSnapshot = c.held ? c.activeSnapshotIndex : -1;
    selectedGeneration = c.activeSnapshotGeneration;
}

PitchSourceRouter::Query PitchSourceRouter::FrameBank::query (std::int64_t from, std::int64_t to) const noexcept
{
    Query result;
    if (from > to || from < first || to > last) return result;
    std::array<float, kPitchFrames> pitches {};
    int present = 0, voiced = 0;
    double clarity = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto& value = frames[(std::size_t) i];
        if (value.histPos < from || value.histPos > to) continue;
        ++present;
        if (value.f0Hz >= 50 && value.f0Hz <= 1000 && value.clarity >= 0.8f)
        {
            pitches[(std::size_t) voiced++] = value.f0Hz;
            clarity += value.clarity;
        }
    }
    result.voicedFraction = present > 0 ? (float) voiced / (float) present : 0;
    if (voiced > 0)
    {
        std::sort (pitches.begin(), pitches.begin() + voiced);
        result.f0Hz = voiced % 2 != 0 ? pitches[(std::size_t) (voiced / 2)]
            : 0.5f * (pitches[(std::size_t) (voiced / 2 - 1)] + pitches[(std::size_t) (voiced / 2)]);
        result.clarity = (float) (clarity / voiced);
        result.voiced = result.voicedFraction >= 0.6f;
    }
    return result;
}

PitchSourceRouter::Query PitchSourceRouter::query (std::int64_t from, std::int64_t to) const noexcept
{
    if (! isActive()) return {};
    if (wasHeld)
    {
        if (selectedSnapshot == -1) return heldHistory.query (from, to);
        if (selectedSnapshot < 0 || selectedSnapshot >= 2 || selectedGeneration == 0) return {};
        const auto& bank = snapshots[(std::size_t) selectedSnapshot];
        return bank.generation == selectedGeneration ? bank.query (from, to) : Query {};
    }
    if (! mapping) return {};
    FrameBank current;
    freeze (current, liveSegmentFirst, historySegmentFirst, expectedHistory - 1, 0);
    return current.query (from, to);
}
} // namespace gapfill::harmonic::psola
