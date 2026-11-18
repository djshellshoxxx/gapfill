#include "CaptureLatch.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gapfill
{

void CaptureLatch::prepare (double sampleRate, int numChannels)
{
    sr = std::max (8000.0, sampleRate);
    channels = std::clamp (numChannels, 1, 2);

    live.prepare (channels, (int) std::ceil ((kMaxSeconds + 0.25) * sr));

    for (auto& s : snapshot)
        s.prepare (channels, (int) std::ceil (kMaxSeconds * sr) + 8);

    scratch.setSize (channels, kMaxCopyPerBlock, false, true, false);
    reset();
}

void CaptureLatch::reset() noexcept
{
    live.clear();

    for (auto& s : snapshot)
        s.clear();

    wasHeld = false;
    suppressed = false;
    recaptureRequested = false;
    pending = false;
    activeIndex = -1;
    targetIndex = 0;
    copyStart = 0;
    copyTotal = copied = 0;
    frozenEnd = 0;
    requestedEnd = 0;
    snapshotGeneration[0] = snapshotGeneration[1] = 0;
    latestCopy = {};
    detailNotice = None;
    lastCopy = maxCopy = 0;
    publish();
}

void CaptureLatch::setLengthMs (float ms) noexcept
{
    lengthMs = std::isfinite (ms) ? std::clamp (ms, (float) (kMinSeconds * 1000.0), (float) (kMaxSeconds * 1000.0)) : 1000.0f;
}

void CaptureLatch::setDetailed (bool enabled) noexcept
{
    if (detailed == enabled) return;
    detailed = enabled;
    pending = recaptureRequested = false;
    latestCopy = {};
    live.clear();
    detailNotice = None;
    publish();
}

int CaptureLatch::maxDetailedChunkSamples() const noexcept
{
    // The first write after a request must fit in the tap's quarter-second
    // margin before copying its oldest requested sample (notably at8kHz).
    return std::min (kMaxCopyPerBlock, std::max (1, (int) std::floor (0.25 * sr) - 16));
}

int CaptureLatch::windowSamples() const noexcept
{
    return std::max (1, (int) std::lround ((double) lengthMs * 0.001 * sr));
}

void CaptureLatch::noteParameter (bool on) noexcept
{
    if (! on)
        suppressed = false;      // a falling edge re-arms Capture after a ReleaseCapture command
}

void CaptureLatch::requestRecapture() noexcept
{
    if (detailed)
    {
        if (pending || recaptureRequested) { detailNotice = Busy; publish(); return; }
        if (! wasHeld || suppressed) { detailNotice = NotHeld; publish(); return; }
        if (live.getWritePosition() < (std::int64_t) std::ceil (kMinSeconds * sr))
        { detailNotice = Short; publish(); return; }
        requestedEnd = live.getWritePosition();
        detailNotice = None;
    }
    recaptureRequested = true;   // consumed by update() while held; a second request replaces a pending copy
}

void CaptureLatch::requestRelease() noexcept
{
    suppressed = true;
    recaptureRequested = false;
}

void CaptureLatch::startCopy() noexcept
{
    const auto end = detailed ? requestedEnd : live.getWritePosition();
    const auto available = (int) std::min<std::int64_t> (end, (std::int64_t) live.getSize() - 8);
    copyTotal = std::min (windowSamples(), available);

    if (copyTotal <= 0)
    {
        pending = false;
        return;
    }

    copyStart = end - copyTotal;
    copied = 0;

    // Copy into the snapshot that is NOT the active one, so running voices never see it change.
    targetIndex = activeIndex == 0 ? 1 : 0;
    if (copySerial == std::numeric_limits<std::uint64_t>::max())
    { pending = false; detailNotice = Overwritten; return; }
    latestCopy = { ++copySerial, targetIndex, copyStart, copyTotal };
    snapshot[targetIndex].clear();
    pending = true;
}

bool CaptureLatch::advance() noexcept
{
    lastCopy = 0;

    if (! pending)
        return false;

    const auto n = std::min (kMaxCopyPerBlock, copyTotal - copied);
    const auto oldestReadable = std::max<std::int64_t> (0, live.getWritePosition() - live.getSize() + 2);
    if (copyStart + copied < oldestReadable)
    {
        pending = false;
        latestCopy = {};
        detailNotice = Overwritten;
        return false; // keep the previous complete source, never publish overwritten PCM
    }
    live.copyTo (scratch, copyStart + copied, n);
    snapshot[targetIndex].write (scratch, 0, n);
    copied += n;
    lastCopy = n;
    maxCopy = std::max (maxCopy, n);

    if (copied < copyTotal)
        return false;

    activeIndex = targetIndex;       // block boundary: the new snapshot becomes the fill source
    snapshotGeneration[activeIndex] = latestCopy.generation;
    pending = false;
    detailNotice = None;
    snapshotCount.fetch_add (1, std::memory_order_relaxed);
    return true;
}

void CaptureLatch::clearHold (SourceHistory& history) noexcept
{
    pending = false;
    recaptureRequested = false;
    activeIndex = -1;
    latestCopy = {};
    copied = copyTotal = 0;
    history.setReadFloor (0);
}

bool CaptureLatch::update (bool held, const juce::AudioBuffer<float>& input, int start, int n,
                           SourceHistory& history) noexcept
{
    bool swapped = false;

    if (held && ! wasHeld)
    {
        frozenEnd = history.getWritePosition();      // freeze point: where the paused writer stands
        activeIndex = -1;
        pending = false;
        if (detailed) live.clear();
        detailNotice = None;
    }
    else if (! held && wasHeld)
    {
        swapped = activeIndex >= 0;                   // back to the live history: the source changes again
        clearHold (history);
        if (detailed) live.clear();
        detailNotice = None;
    }

    if (! detailed || held) live.write (input, start, n);

    if (! held)
        recaptureRequested = false;                   // nothing to recapture while Capture is off

    wasHeld = held;

    if (held)
    {
        if (recaptureRequested)
        {
            recaptureRequested = false;
            startCopy();                              // replaces any pending copy
        }

        swapped = advance() || swapped;

        // Until a snapshot replaces it, the frozen history is limited to captureLength before the freeze.
        history.setReadFloor (activeIndex < 0 ? std::max<std::int64_t> (0, frozenEnd - windowSamples()) : 0);
    }

    publish();
    return swapped;
}

void CaptureLatch::publish() noexcept
{
    status.store (! wasHeld ? Off : (pending ? Pending : (detailed && activeIndex >= 0 ? HeldRecaptured : Held)), std::memory_order_relaxed);
    notice.store (detailNotice, std::memory_order_relaxed);
    remainingHoldMs.store ((int) std::ceil (1000.0 * std::max (0.0, kMinSeconds
        - (double) live.getWritePosition() / sr)), std::memory_order_relaxed);
    progress.store (pending && copyTotal > 0 ? (float) copied / (float) copyTotal : 0.0f, std::memory_order_relaxed);
    heldMs.store (wasHeld ? (int) std::lround (1000.0 * (double) (pending || activeIndex < 0 ? windowSamples()
                                                                       : (int) snapshot[std::max (0, activeIndex)].getWritePosition()) / sr)
                          : 0,
                  std::memory_order_relaxed);
}

} // namespace gapfill
