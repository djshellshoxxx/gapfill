#include "ScDrive.h"

#include <algorithm>
#include <cmath>

namespace gapfill::scdrive
{

void SidechainOnsetTracker::prepare (double sampleRate)
{
    sr = sampleRate > 8000.0 ? sampleRate : 8000.0;
    window = std::max (1, (int) std::lround ((double) kRmsMs * 0.001 * sr));
    floorSlots = std::max (2, (int) std::lround ((double) kFloorMs * 0.001 * sr / (double) kSlot));
    refractory = (std::int64_t) ((double) kRefractoryMs * 0.001 * sr);
    squares.assign ((size_t) window, 0.0f);
    levels.assign ((size_t) kSlots, kSilenceDb);
    reset();
}

void SidechainOnsetTracker::reset() noexcept
{
    std::fill (squares.begin(), squares.end(), 0.0f);
    std::fill (levels.begin(), levels.end(), kSilenceDb);
    squareIdx = 0;
    accum = 0.0;
    nextPos = -1;
    newestSlot = -1;
    haveData = false;
    armed = true;
    lastOnset = -1000000000;
    numOnsets = 0;
    onsetHead = 0;
    onsets.fill (-1);
}

void SidechainOnsetTracker::process (const float* mono, int n, std::int64_t inputPos, float thresholdDb) noexcept
{
    if (levels.empty() || n <= 0)
        return;

    if (inputPos != nextPos)
    {
        // The stream was not continuous (feature switched off, transport restart): start over.
        std::fill (levels.begin(), levels.end(), kSilenceDb);
        std::fill (squares.begin(), squares.end(), 0.0f);
        accum = 0.0;
        squareIdx = 0;
        haveData = false;
        newestSlot = -1;
        armed = true;
        numOnsets = 0;
        onsetHead = 0;
        onsets.fill (-1);
    }

    for (int i = 0; i < n; ++i)
    {
        const auto pos = inputPos + i;
        const auto x = mono != nullptr ? mono[i] : 0.0f;
        const auto sq = std::isfinite (x) ? x * x : 0.0f;

        accum += (double) sq - (double) squares[(size_t) squareIdx];
        squares[(size_t) squareIdx] = sq;

        if (++squareIdx >= window)
            squareIdx = 0;

        if ((pos % kSlot) == kSlot - 1)
            closeSlot (pos / kSlot, thresholdDb);
    }

    nextPos = inputPos + n;
}

void SidechainOnsetTracker::closeSlot (std::int64_t slot, float thresholdDb) noexcept
{
    const auto ms = std::max (0.0, accum) / (double) window;
    const auto db = std::max (kSilenceDb, (float) (10.0 * std::log10 (ms + 1.0e-12)));

    float floorDb = db;
    if (! haveData)
        firstSlot = slot;

    const auto oldest = std::max<std::int64_t> (firstSlot, slot - floorSlots);

    for (auto s = oldest; s < slot; ++s)
        floorDb = std::min (floorDb, levels[(size_t) (s % kSlots)]);

    levels[(size_t) (slot % kSlots)] = db;
    newestSlot = slot;
    haveData = true;

    if (db < floorDb + kOnsetRiseDb * 0.5f)
        armed = true;

    const auto startPos = slot * kSlot;

    if (armed && db > floorDb + kOnsetRiseDb && db > thresholdDb && startPos - lastOnset >= refractory)
    {
        armed = false;
        lastOnset = startPos;
        onsets[(size_t) onsetHead] = startPos;
        onsetHead = (onsetHead + 1) % kOnsets;
        numOnsets = std::min (numOnsets + 1, kOnsets);
    }
}

bool SidechainOnsetTracker::covers (std::int64_t pos) const noexcept
{
    if (! haveData || pos < 0)
        return false;

    const auto slot = pos / kSlot;
    return slot <= newestSlot && slot > newestSlot - kSlots && slot >= firstSlot;
}

float SidechainOnsetTracker::levelDbAt (std::int64_t pos) const noexcept
{
    return covers (pos) ? levels[(size_t) ((pos / kSlot) % kSlots)] : kSilenceDb;
}

std::int64_t SidechainOnsetTracker::nearestOnsetWithin (std::int64_t from, std::int64_t windowSamples) const noexcept
{
    std::int64_t best = -1;

    for (int i = 0; i < numOnsets; ++i)
    {
        const auto o = onsets[(size_t) i];

        if (o >= from && o <= from + windowSamples && (best < 0 || o < best))
            best = o;
    }

    return best;
}

RejectReason evaluateGate (const ScSettings& sc, std::int64_t pos) noexcept
{
    if (sc.tracker == nullptr || sc.gate == GateOff || ! sc.tracker->covers (pos))
        return RejectReason::None;

    const bool above = sc.tracker->levelDbAt (pos) >= sc.threshDb;
    const bool open = sc.gate == GateWhenQuiet ? ! above : above;
    return open ? RejectReason::None : RejectReason::ScGated;
}

} // namespace gapfill::scdrive
