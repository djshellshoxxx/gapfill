#pragma once

// U-6 / F-L05 audio-thread recorder (docs/dev/features/F-L05-auto-calibrate.md).
//
// The processor calls write() once per chunk with the live mono input sum. While idle that is a
// single relaxed atomic load. The buffer (15 s x sample rate) is allocated lazily on the MESSAGE
// thread by allocate(); if it is not ready start() refuses ("Try again") instead of allocating.
//
// Threading: allocate / start / cancel / snapshot = message thread; write = audio thread.
// start() publishes the buffer with a release store of the state; write() never allocates.
// A nonblocking ownership claim protects storage and recording metadata. After cancellation,
// allocate()/start() return false until an already-entered writer has returned. The audio
// thread makes one claim attempt, never waits, and rechecks Recording after acquiring it.

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
    static_assert (std::atomic<bool>::is_always_lock_free,
                   "RecordTap storage ownership must be lock-free on the audio thread");

public:
    enum class State : int { Idle = 0, Recording, Done, Cancelled };

    /** Message thread. False while storage is owned by a cancelled writer; retry later. */
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

        StorageClaim claim (storageClaimed);

        if (! claim || state.load (std::memory_order_acquire) != (int) State::Recording)
            return;

       #ifdef GAPFILL_UNIT_TESTS
        if (beforeWriteForTests != nullptr)
            beforeWriteForTests (writeContextForTests);
       #endif

        writeRecording (monoMain, numSamples);
    }

   #ifdef GAPFILL_UNIT_TESTS
    /** Install before starting the audio worker; unchanged until that worker joins. */
    void setBeforeWriteForTests (void (*callback) (void*), void* context) noexcept
    {
        beforeWriteForTests = callback;
        writeContextForTests = context;
    }
   #endif

    State getState() const noexcept          { return (State) state.load (std::memory_order_acquire); }
    std::int64_t getFramesWritten() const noexcept   { return written.load (std::memory_order_acquire); }
    std::int64_t getTargetFrames() const noexcept    { return target.load (std::memory_order_relaxed); }
    double getAllocatedRate() const noexcept { return allocatedRate; }

    /** Message thread, only when Done: copy of the recording (the buffer can then be reused). */
    std::vector<float> snapshot() const;

private:
    class StorageClaim
    {
    public:
        explicit StorageClaim (std::atomic<bool>& owner) noexcept : claimed (owner)
        {
            bool expected = false;
            ownsStorage = claimed.compare_exchange_strong (expected, true, std::memory_order_acquire);
        }

        ~StorageClaim()
        {
            if (ownsStorage)
                claimed.store (false, std::memory_order_release);
        }

        explicit operator bool() const noexcept { return ownsStorage; }
        StorageClaim (const StorageClaim&) = delete;
        StorageClaim& operator= (const StorageClaim&) = delete;

    private:
        std::atomic<bool>& claimed;
        bool ownsStorage = false;
    };

    void writeRecording (const float*, int) noexcept;

    std::vector<float> buffer;
    double allocatedRate = 0.0;
    std::atomic<bool> ready { false };
    std::atomic<int> state { (int) State::Idle };
    std::atomic<std::int64_t> written { 0 }, target { 0 };
    std::atomic<bool> storageClaimed { false };

   #ifdef GAPFILL_UNIT_TESTS
    void (*beforeWriteForTests) (void*) = nullptr;
    void* writeContextForTests = nullptr;
   #endif
};

} // namespace gapfill::calibrate
