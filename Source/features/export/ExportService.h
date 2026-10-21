#pragma once

// U-2 Fill Export: the wet-bus tap and the one-slot export mailbox
// (docs/dev/05-SPEC-USABILITY-VALUE.md U-2, docs/dev/10 section 4.5).
//
// What is exported: the WET bus, i.e. only what GapFill generated, after Low/Centre
// protect and width and before the dry/wet mix.
//
// Contract
//  * prepare() (message thread, from prepareToPlay) allocates everything: a stereo
//    ring of 8 s and an export buffer for one segment (<= 3.2 s). Nothing is
//    allocated afterwards on any thread that matters.
//  * The tap is written once per chunk by the audio thread ONLY while armed. The
//    editor arms it (arm()) and keeps re-arming while a recipe drawer exists; it
//    disarms itself 60 s after the last arm(). Disarmed means zero extra work.
//  * A request {startPos, lengthSamples} (absolute output positions, as reported by
//    FillEvent) goes through a one-slot mailbox: Idle -> Requested -> Copying ->
//    Ready (or Failed). One request at a time: a second one while busy is Busy.
//    The audio thread copies at most 4096 samples per block; the message thread
//    reads the buffer only when Ready and then sets Idle.
//  * The WAV is written on the message thread (WavExport.h); file I/O never happens
//    on the audio thread. Errors are returned, never swallowed.
//  * A sample-rate change (prepare) cancels any request and clears the ring.
//  * Segments: 20 ms lead-in + the planned length + 30 ms tail. "Too old" when the
//    segment starts before the ring or before the tap was armed.

#include "../../dsp/EventLogs.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>

namespace gapfill::exporting
{

inline constexpr double kRingSeconds       = 8.0;
inline constexpr double kLeadInSeconds     = 0.020;
inline constexpr double kTailSeconds       = 0.030;
inline constexpr double kMaxSegmentSeconds = 3.2;     ///< voice cap 3 s + lead-in + tail
inline constexpr int    kCopyPerBlock      = 4096;
inline constexpr std::uint32_t kDisarmAfterMs = 60000;

enum class ExportState : int { Idle = 0, Requested, Copying, Ready, Failed };

enum class RequestResult { Accepted, Busy, NoSpan, NotPrepared };

enum class Failure : int { None = 0, TooOld, NotRecorded, TooLong };

class ExportService
{
public:
    //==========================================================================
    // Message thread

    /** Allocates the ring and the export buffer. Cancels everything in flight. */
    void prepare (double sampleRate);

    /** Starts (or extends) the 60 s armed window. Cheap; call as often as needed. */
    void arm() noexcept                       { armForMs (kDisarmAfterMs); }
    void armForMs (std::uint32_t ms) noexcept;
    void disarm() noexcept                    { armed.store (false, std::memory_order_relaxed); }
    bool isArmed() const noexcept       { return armed.load (std::memory_order_relaxed); }

    /** Asks for a segment. Never blocks. */
    RequestResult request (const FillSpan& span) noexcept;

    /** Cancels a request the audio thread has not started copying. */
    bool cancelPending() noexcept;

    ExportState getState() const noexcept       { return (ExportState) state.load (std::memory_order_acquire); }

    /** When Ready: moves the segment into `out` (resized by this call), returns true and
        goes back to Idle. When Failed: returns false, fills `why`, goes back to Idle.
        Returns false with `why == None` while still waiting. */
    bool take (juce::AudioBuffer<float>& out, Failure& why);

    double getSampleRate() const noexcept       { return sr; }

    std::atomic<float> bpm { 0.0f };            ///< published by the processor, for file names

    //==========================================================================
    // Audio thread

    /** Writes `numSamples` of the wet bus (starting at `startSample`) whose first sample
        is output position `outputPos`, and services the mailbox. Allocation free. */
    void process (const juce::AudioBuffer<float>& wet, int startSample, int numSamples,
                  std::int64_t outputPos) noexcept;

    /** Test hooks. */
    std::int64_t getWriteEnd() const noexcept   { return writeEnd; }
    std::int64_t getFirstPos() const noexcept   { return firstPos; }
    int getRingSize() const noexcept            { return ringSize; }
    long getSamplesWritten() const noexcept     { return samplesWritten.load (std::memory_order_relaxed); }
    int getMaxCopyPerBlock() const noexcept     { return maxCopyObserved.load (std::memory_order_relaxed); }

private:
    double sr = 0.0;
    int ringSize = 0;
    int segmentCapacity = 0;
    juce::AudioBuffer<float> ring;
    juce::AudioBuffer<float> segment;

    // audio-thread owned
    std::int64_t writeEnd = 0;          ///< next absolute position expected
    std::int64_t firstPos = -1;         ///< first position written since arming / discontinuity
    bool tapActive = false;             ///< was armed at the last block
    int copyCursor = 0, copyTotal = 0;
    std::int64_t copyStart = 0;

    // message -> audio
    std::atomic<bool> armed { false };
    std::atomic<std::uint32_t> armedUntil { 0 };
    std::atomic<int> state { (int) ExportState::Idle };
    std::int64_t reqStart = 0;          ///< published by state = Requested (release)
    int reqLength = 0;

    std::atomic<int> failure { 0 };
    std::atomic<int> resultSamples { 0 };
    std::atomic<long> samplesWritten { 0 };
    std::atomic<int> maxCopyObserved { 0 };
};

} // namespace gapfill::exporting
