#include "Panic.h"

#include <algorithm>
#include <cmath>

namespace gapfill::panic
{

void PanicController::setOptions (const Options& o) noexcept
{
    recoveryMode.store ((std::uint8_t) o.recovery, std::memory_order_relaxed);
    toggles.store (o.panicToggles, std::memory_order_relaxed);
}

Options PanicController::getOptions() const noexcept
{
    Options o;
    o.recovery = (RecoveryMode) std::min<int> (3, recoveryMode.load (std::memory_order_relaxed));
    o.panicToggles = toggles.load (std::memory_order_relaxed);
    return o;
}

void PanicController::prepare (double sampleRate, int maxChunk)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    gains.assign ((size_t) std::max (1, maxChunk), 1.0f);
    reset();
}

void PanicController::reset() noexcept
{
    phase = Phase::Idle;
    gain = 1.0f;
    blockPos = 0;
    pendingPanicAt = -1;
    panickedSamples = 0;
    seenPanic = panicSeq.load (std::memory_order_acquire);
    seenRecover = recoverSeq.load (std::memory_order_acquire);
    panicked.store (false, std::memory_order_release);
}

Action PanicController::beginBlock() noexcept
{
    blockPos = 0;
    Action result = Action::None;

    const auto r = recoverSeq.load (std::memory_order_acquire);

    if (r != seenRecover)
    {
        seenRecover = r;

        if (recover() == Action::Recover)
            result = Action::Recover;
    }

    const auto p = panicSeq.load (std::memory_order_acquire);

    if (p != seenPanic)
    {
        seenPanic = p;

        if (const auto a = press (0); a != Action::None)
            result = a;
    }

    return result;
}

Action PanicController::press (int sampleOffset) noexcept
{
    if (panicked.load (std::memory_order_relaxed))
        return toggles.load (std::memory_order_relaxed) ? recover() : Action::None;

    pendingPanicAt = std::max (0, sampleOffset);
    panickedSamples = 0;
    panicked.store (true, std::memory_order_release);
    return Action::Panic;
}

Action PanicController::recover() noexcept
{
    if (! panicked.load (std::memory_order_relaxed))
        return Action::None;

    panicked.store (false, std::memory_order_release);

    if (pendingPanicAt >= 0)
    {
        // The panic never started ramping (same block): nothing to ramp back.
        pendingPanicAt = -1;
        return Action::Recover;
    }

    startRampUp();
    return Action::Recover;
}

void PanicController::startRampDown() noexcept
{
    phase = Phase::RampDown;
    rampLen = std::max (1, (int) std::lround (kRampDownSeconds * sr));
    rampLeft = rampLen;
    rampFrom = gain;
}

void PanicController::startRampUp() noexcept
{
    phase = Phase::RampUp;
    rampLen = std::max (1, (int) std::lround (kRampUpSeconds * sr));
    rampLeft = rampLen;
    rampFrom = gain;
}

const float* PanicController::nextGains (int n) noexcept
{
    n = std::min (n, (int) gains.size());

    std::int64_t autoSamples = 0;

    switch ((RecoveryMode) recoveryMode.load (std::memory_order_relaxed))
    {
        case RecoveryMode::Auto1s: autoSamples = (std::int64_t) std::lround (1.0 * sr); break;
        case RecoveryMode::Auto2s: autoSamples = (std::int64_t) std::lround (2.0 * sr); break;
        case RecoveryMode::Auto4s: autoSamples = (std::int64_t) std::lround (4.0 * sr); break;
        default: break;
    }

    for (int i = 0; i < n; ++i, ++blockPos)
    {
        if (pendingPanicAt >= 0 && blockPos >= pendingPanicAt)
        {
            pendingPanicAt = -1;
            startRampDown();
        }

        if (phase == Phase::Idle)
        {
            gains[(size_t) i] = 1.0f;
            continue;
        }

        if (phase == Phase::RampDown || phase == Phase::Silent)
        {
            ++panickedSamples;

            if (autoSamples > 0 && panickedSamples >= autoSamples && panicked.load (std::memory_order_relaxed))
            {
                panicked.store (false, std::memory_order_release);
                startRampUp();
            }
        }

        switch (phase)
        {
            case Phase::RampDown:
                gain = rampFrom * (float) rampLeft / (float) rampLen;
                gains[(size_t) i] = gain;

                if (--rampLeft < 0)
                {
                    gain = 0.0f;
                    phase = Phase::Silent;
                }
                break;

            case Phase::Silent:
                gain = 0.0f;
                gains[(size_t) i] = 0.0f;
                break;

            case Phase::RampUp:
            {
                const auto k = (float) (rampLen - rampLeft);
                gain = rampFrom + (1.0f - rampFrom) * k / (float) rampLen;
                gains[(size_t) i] = gain;

                if (--rampLeft <= 0)
                {
                    gain = 1.0f;
                    phase = Phase::Idle;
                }
                break;
            }

            case Phase::Idle:
                break;
        }
    }

    return gains.data();
}

} // namespace gapfill::panic
