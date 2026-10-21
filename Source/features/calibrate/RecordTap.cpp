#include "RecordTap.h"

#include <algorithm>
#include <cmath>

namespace gapfill::calibrate
{

bool RecordTap::allocate (double sampleRate)
{
    if (sampleRate <= 0.0)
        return false;

    if (ready.load (std::memory_order_acquire) && allocatedRate == sampleRate)
        return true;

    // A running recording must not lose its buffer under the audio thread.
    if (getState() == State::Recording)
        return false;

    ready.store (false, std::memory_order_release);
    buffer.assign ((size_t) std::ceil (sampleRate * kMaxSeconds), 0.0f);
    allocatedRate = sampleRate;
    ready.store (true, std::memory_order_release);
    return true;
}

bool RecordTap::start (int seconds, double sampleRate) noexcept
{
    if (! isReadyFor (sampleRate) || getState() == State::Recording)
        return false;

    seconds = std::clamp (seconds, kMinSeconds, kMaxSeconds);
    written.store (0, std::memory_order_relaxed);
    target.store ((std::int64_t) std::llround (sampleRate * seconds), std::memory_order_relaxed);
    state.store ((int) State::Recording, std::memory_order_release);
    return true;
}

void RecordTap::cancel() noexcept
{
    int expected = (int) State::Recording;
    state.compare_exchange_strong (expected, (int) State::Cancelled, std::memory_order_acq_rel);
}

void RecordTap::reset() noexcept
{
    if (getState() != State::Recording)
        state.store ((int) State::Idle, std::memory_order_release);
}

void RecordTap::writeRecording (const float* in, int n) noexcept
{
    const auto done = written.load (std::memory_order_relaxed);
    const auto want = target.load (std::memory_order_relaxed);
    const auto room = std::min<std::int64_t> ((std::int64_t) buffer.size() - done, want - done);
    const auto count = (int) std::min<std::int64_t> (n, std::max<std::int64_t> (0, room));

    if (count > 0)
        std::copy (in, in + count, buffer.begin() + (std::ptrdiff_t) done);

    const auto total = done + count;
    written.store (total, std::memory_order_release);

    if (total >= want)
        state.store ((int) State::Done, std::memory_order_release);
}

std::vector<float> RecordTap::snapshot() const
{
    if (getState() != State::Done)
        return {};

    const auto n = (size_t) written.load (std::memory_order_acquire);
    return std::vector<float> (buffer.begin(), buffer.begin() + (std::ptrdiff_t) std::min (n, buffer.size()));
}

} // namespace gapfill::calibrate
