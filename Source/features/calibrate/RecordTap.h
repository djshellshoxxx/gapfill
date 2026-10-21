#pragma once

// U-6 / F-L05 audio-thread recorder (docs/dev/features/F-L05-auto-calibrate.md).
//
// The processor calls write() once per chunk with the live mono input sum. While idle that is a
// single relaxed atomic load. The buffer (15 s x sample rate) is allocated lazily on the MESSAGE
// thread by allocate(); if it is not ready start() refuses ("Try again") instead of allocating.
//
// Threading: allocate / start / cancel / snapshot = message thread; write = audio thread.
// start() publishes the buffer with a release store of the state; write() never allocates.

#include <atomic>
#include <cstdint>
#include <vector>

namespace gapfill::calibrate
{

inline constexpr int kMinSeconds = 4;
inline constexpr int kMaxSeconds = 15;
inline constexpr int kDefaultSeconds = 8;

class RecordTap
{
public:
    enum class State : int { Idle = 0, Recording, Done, Cancelled };

    /** Message thread. Allocates for `sampleRate`; a different rate drops the old buffer. */
    bool allocate (double sampleRate);
    bool isReadyFor (double sampleRate) const noexcept   { return ready.load (std::memory_order_acquire) && allocatedRate == sampleRate; }

    /** Message thread. False when the buffer is not ready for `sampleRate` or a recording is running. */
    bool start (int seconds, double sampleRate) noexcept;
    void cancel() noexcept;
    void reset() noexcept;

    /** Audio thread. Bounded copy of `numSamples` floats while Recording, otherwise one atomic load. */
    void write (const float* monoMain, int numSamples) noexcept
    {
        if (state.load (std::memory_order_acquire) != (int) State::Recording)
            return;

        writeRecording (monoMain, numSamples);
    }

    State getState() const noexcept          { return (State) state.load (std::memory_order_acquire); }
    std::int64_t getFramesWritten() const noexcept   { return written.load (std::memory_order_acquire); }
    std::int64_t getTargetFrames() const noexcept    { return target.load (std::memory_order_relaxed); }
    double getAllocatedRate() const noexcept { return allocatedRate; }

    /** Message thread, only when Done: copy of the recording (the buffer can then be reused). */
    std::vector<float> snapshot() const;

private:
    void writeRecording (const float*, int) noexcept;

    std::vector<float> buffer;
    double allocatedRate = 0.0;
    std::atomic<bool> ready { false };
    std::atomic<int> state { (int) State::Idle };
    std::atomic<std::int64_t> written { 0 }, target { 0 };
};

} // namespace gapfill::calibrate
