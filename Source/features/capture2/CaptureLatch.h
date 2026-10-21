#pragma once

// C-10 Capture v2 (docs/dev/04-SPEC-CREATIVE.md C-10, creative spec section 8
// "Capture, queue, and history bounds").
//
// Capture v1 (0.0.1) pauses the source-history writer while the `capture`
// parameter is on, so fills keep reading the frozen material. That behaviour is
// unchanged. Capture v2 adds:
//
//   * captureLength (250-2000 ms): the window before the freeze point that fills may
//     read; older material reads as unwritten (SourceHistory read floor).
//   * Recapture: refreshes the latched material to the latest captureLength of live
//     input while Capture stays on. A separate always-recording live tap (2.25 s)
//     is copied incrementally (<= 4096 samples per block) into the inactive one
//     of two 2 s snapshots; while the copy runs the previous latch stays active
//     (state Pending). When complete, the snapshot becomes the fill source at a
//     block boundary; voices that read the source while rendering (Granular) are
//     faded out over <= 10 ms by the engine (FillEngine::sourceChanged).
//   * Release (parameter off, or the ReleaseCapture command) cancels a pending copy
//     and returns to the live history. prepare() / reset() clear everything.
//
// Audio-thread owned; the status atomics are read by the editor. Memory is
// allocated in prepare() only: live tap 2.25 s + two snapshots 2 s + a 4096-sample
// scratch, per channel.

#include "../../dsp/DspUtils.h"

#include <atomic>
#include <cstdint>

namespace gapfill
{

class CaptureLatch
{
public:
    static constexpr int kMaxCopyPerBlock = 4096;
    static constexpr double kMaxSeconds = 2.0;
    static constexpr double kMinSeconds = 0.25;

    enum Status : int { Off = 0, Held = 1, Pending = 2 };

    void prepare (double sampleRate, int channels);
    void reset() noexcept;

    /** The captureLength parameter in milliseconds (clamped to 250-2000). */
    void setLengthMs (float ms) noexcept;

    /** Tracks the Capture parameter edges; a falling edge clears a ReleaseCapture suppression. */
    void noteParameter (bool captureParameterOn) noexcept;
    bool isSuppressed() const noexcept   { return suppressed; }

    /** Commands 7 (Recapture) and 8 (ReleaseCapture), audio thread. */
    void requestRecapture() noexcept;
    void requestRelease() noexcept;

    /** Once per processed chunk: records `n` samples of the live input into the live tap, handles the
        engage / release edges for `held` (the history writer is paused exactly when `held`), advances a
        pending copy and keeps the history read floor in step with captureLength.
        Returns true when a recapture snapshot became the fill source in this call. */
    bool update (bool held, const juce::AudioBuffer<float>& input, int start, int n, SourceHistory& history) noexcept;

    /** The snapshot the engine must read, or nullptr to read the (live or frozen) history. */
    const SourceHistory* getActiveSnapshot() const noexcept   { return activeIndex >= 0 ? &snapshot[activeIndex] : nullptr; }

    //==========================================================================
    // Status (relaxed atomics; message thread)
    int   getStatus() const noexcept        { return status.load (std::memory_order_relaxed); }
    float getProgress() const noexcept      { return progress.load (std::memory_order_relaxed); }   ///< 0..1 of a pending copy
    int   getHeldMs() const noexcept        { return heldMs.load (std::memory_order_relaxed); }
    int   getSnapshotCount() const noexcept { return snapshotCount.load (std::memory_order_relaxed); }

    //==========================================================================
    // Test hooks
    int  getLastCopyCount() const noexcept            { return lastCopy; }
    int  getMaxCopyCount() const noexcept             { return maxCopy; }
    bool isPending() const noexcept                   { return pending; }
    bool isHeld() const noexcept                      { return wasHeld; }
    const SourceHistory& getLiveTap() const noexcept  { return live; }
    int  getWindowSamples() const noexcept            { return windowSamples(); }

private:
    int  windowSamples() const noexcept;
    void startCopy() noexcept;
    bool advance() noexcept;
    void clearHold (SourceHistory& history) noexcept;
    void publish() noexcept;

    double sr = 48000.0;
    int channels = 2;
    float lengthMs = 1000.0f;

    SourceHistory live;                 // always recording, 2.25 s
    SourceHistory snapshot[2];          // 2 s each, ping-pong
    juce::AudioBuffer<float> scratch;

    bool wasHeld = false;
    bool suppressed = false;
    bool recaptureRequested = false;
    bool pending = false;
    int  activeIndex = -1;              // -1: the engine reads `history`
    int  targetIndex = 0;
    std::int64_t copyStart = 0;
    int  copyTotal = 0, copied = 0;
    std::int64_t frozenEnd = 0;         // history write position at the freeze
    int  lastCopy = 0, maxCopy = 0;

    std::atomic<int>   status { Off };
    std::atomic<float> progress { 0.0f };
    std::atomic<int>   heldMs { 0 };
    std::atomic<int>   snapshotCount { 0 };
};

} // namespace gapfill
