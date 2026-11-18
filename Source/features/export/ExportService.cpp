#include "ExportService.h"
#include <limits>

namespace gapfill::exporting
{

void ExportService::prepare (double sampleRate)
{
    const std::lock_guard lock (configuration);
   #ifdef GAPFILL_UNIT_TESTS
    if (probe != nullptr) probe (probeContext, ProbePoint::BeforePrepare);
   #endif
    sr = juce::jmax (8000.0, sampleRate);
    ringSize = (int) std::ceil (sr * kRingSeconds);
    segmentCapacity = (int) std::ceil (sr * kMaxSegmentSeconds);

    ring.setSize (2, ringSize, false, true, false);
    segment.setSize (2, segmentCapacity, false, true, false);
    ring.clear();
    segment.clear();

    // A sample-rate change (or any prepare) cancels everything in flight.
    state.store ((int) ExportState::Idle, std::memory_order_release);
    failure.store ((int) Failure::None, std::memory_order_relaxed);
    writeEnd = 0;
    firstPos = -1;
    tapActive = false;
    copyCursor = copyTotal = 0;
    copyGeneration = copyTapGeneration = 0;
    tapGeneration = 1;
    copyStarted = false;
    activeGeneration.store (++nextGeneration, std::memory_order_release);
    publishedRate.store (sr, std::memory_order_release);
    samplesWritten.store (0, std::memory_order_relaxed);
    maxCopyObserved.store (0, std::memory_order_relaxed);
}

void ExportService::armForMs (std::uint32_t ms) noexcept
{
    armedUntil.store (juce::Time::getMillisecondCounter() + ms, std::memory_order_relaxed);
    armed.store (true, std::memory_order_relaxed);
}

RequestResult ExportService::request (const FillSpan& span) noexcept
{
    const std::unique_lock lock (configuration, std::try_to_lock);
    if (! lock.owns_lock())
        return RequestResult::Busy;
    if (ringSize == 0)
        return RequestResult::NotPrepared;

    if (! span.isKnown())
        return RequestResult::NoSpan;

    retireCancelledResult();
    // Only the message thread leaves Idle. Audio claims Requested with CAS
    // before reading these fields, so cancellation cannot race that read.
    if (state.load (std::memory_order_acquire) != (int) ExportState::Idle)
        return RequestResult::Busy;

    reqStart = span.startPos;
    reqLength = span.lengthSamples;
    reqGeneration = ++nextGeneration;
    activeGeneration.store (reqGeneration, std::memory_order_relaxed);

    int expected = (int) ExportState::Idle;

    return state.compare_exchange_strong (expected, (int) ExportState::Requested, std::memory_order_release)
               ? RequestResult::Accepted : RequestResult::Busy;
}

bool ExportService::cancelPending() noexcept
{
    const std::unique_lock lock (configuration, std::try_to_lock);
    if (! lock.owns_lock())
        return false;
    int expected = (int) ExportState::Requested;
    return state.compare_exchange_strong (expected, (int) ExportState::Idle, std::memory_order_acq_rel);
}

void ExportService::retireCancelledResult() noexcept
{
    if (cancelledGeneration.load (std::memory_order_acquire) != activeGeneration.load (std::memory_order_relaxed))
        return;
    auto current = state.load (std::memory_order_acquire);
    if (current == (int) ExportState::Ready || current == (int) ExportState::Failed)
        state.compare_exchange_strong (current, (int) ExportState::Idle, std::memory_order_acq_rel);
}

bool ExportService::cancel() noexcept
{
    const std::unique_lock lock (configuration, std::try_to_lock);
    if (! lock.owns_lock())
        return false;
    for (;;)
    {
        auto current = state.load (std::memory_order_acquire);
        if (current == (int) ExportState::Idle)
            return false;
        if (current == (int) ExportState::Requested)
        {
            if (state.compare_exchange_strong (current, (int) ExportState::Idle, std::memory_order_acq_rel))
                return true;
            continue; // audio may have claimed it; cancel through its acknowledgement
        }
        cancelledGeneration.store (activeGeneration.load (std::memory_order_relaxed), std::memory_order_release);
        retireCancelledResult();
        return true;
    }
}

void ExportService::failCopy (Failure why) noexcept
{
    failure.store ((int) why, std::memory_order_relaxed);
    state.store ((int) ExportState::Failed, std::memory_order_release);
}

bool ExportService::take (juce::AudioBuffer<float>& out, Failure& why)
{
    return takeResult (out, why, nullptr);
}

bool ExportService::take (juce::AudioBuffer<float>& out, Failure& why, ResultInfo& info)
{
    return takeResult (out, why, &info);
}

bool ExportService::takeResult (juce::AudioBuffer<float>& out, Failure& why, ResultInfo* info)
{
    why = Failure::None;
    const std::unique_lock lock (configuration, std::try_to_lock);
    if (! lock.owns_lock())
        return false;
    const auto s = (ExportState) state.load (std::memory_order_acquire);

    if (s == ExportState::Ready)
    {
        if (cancelledGeneration.load (std::memory_order_acquire) == activeGeneration.load (std::memory_order_relaxed))
        {
            state.store ((int) ExportState::Idle, std::memory_order_release);
            why = Failure::Cancelled;
            return false;
        }
        const auto n = resultSamples.load (std::memory_order_relaxed);
        out.setSize (2, n, false, false, true);

        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, 0, segment, ch, 0, n);

        if (info != nullptr) *info = { sr, copyGeneration };
        state.store ((int) ExportState::Idle, std::memory_order_release);
        return true;
    }

    if (s == ExportState::Failed)
    {
        why = (Failure) failure.load (std::memory_order_relaxed);
        state.store ((int) ExportState::Idle, std::memory_order_release);
    }

    return false;
}

//==============================================================================
void ExportService::process (const juce::AudioBuffer<float>& wet, int startSample, int numSamples,
                             std::int64_t outputPos) noexcept
{
    if (ringSize == 0)
        return;

    // ---- the tap -----------------------------------------------------------
    bool isArmed = armed.load (std::memory_order_relaxed);

    if (isArmed && (std::int32_t) (juce::Time::getMillisecondCounter() - armedUntil.load (std::memory_order_relaxed)) > 0)
    {
        armed.store (false, std::memory_order_relaxed);
        isArmed = false;
    }

    const bool validInput = numSamples > 0 && numSamples <= ringSize && outputPos >= 0
                         && outputPos <= std::numeric_limits<std::int64_t>::max() - numSamples
                         && wet.getNumChannels() > 0 && startSample >= 0
                         && numSamples <= wet.getNumSamples() && startSample <= wet.getNumSamples() - numSamples;
    if (isArmed && validInput)
    {
        if (! tapActive)
        {
            tapActive = true;
            firstPos = -1;
        }

        // A new continuous run starts at arming and after any jump of the output timeline.
        if (firstPos < 0 || outputPos != writeEnd)
        {
            firstPos = outputPos;
            ++tapGeneration;
        }

        const auto chans = juce::jmin (2, wet.getNumChannels());
        const auto at = (int) (outputPos % ringSize);
        const auto first = juce::jmin (numSamples, ringSize - at);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* src = wet.getReadPointer (juce::jmin (ch, chans - 1), startSample);
            auto* dst = ring.getWritePointer (ch);
            juce::FloatVectorOperations::copy (dst + at, src, first);

            if (first < numSamples)
                juce::FloatVectorOperations::copy (dst, src + first, numSamples - first);
        }

        writeEnd = outputPos + numSamples;
        samplesWritten.fetch_add (numSamples, std::memory_order_relaxed);
    }
    else if (! isArmed || numSamples > 0)
    {
        if (tapActive) ++tapGeneration;
        tapActive = false;
    }

    // ---- the mailbox ---------------------------------------------------------
    auto s = (ExportState) state.load (std::memory_order_acquire);

    if (s == ExportState::Requested)
    {
       #ifdef GAPFILL_UNIT_TESTS
        if (probe != nullptr) probe (probeContext, ProbePoint::BeforeClaim);
       #endif
        int expected = (int) ExportState::Requested;
        if (! state.compare_exchange_strong (expected, (int) ExportState::Claimed, std::memory_order_acq_rel))
            return;
       #ifdef GAPFILL_UNIT_TESTS
        if (probe != nullptr) probe (probeContext, ProbePoint::AfterClaim);
       #endif
        const auto lead = (std::int64_t) std::llround (kLeadInSeconds * sr);
        const auto tail = (std::int64_t) std::llround (kTailSeconds * sr);
        copyGeneration = reqGeneration;
        if (cancelledGeneration.load (std::memory_order_acquire) == copyGeneration)
        {
            state.store ((int) ExportState::Idle, std::memory_order_release);
            return;
        }
        const auto total = (std::int64_t) reqLength + lead + tail;
        if (reqLength <= 0 || total > segmentCapacity)
        {
            failCopy (Failure::TooLong);
            return;
        }
        if (reqStart < lead || reqStart > std::numeric_limits<std::int64_t>::max() - reqLength - tail)
        {
            failCopy (Failure::NotRecorded);
            return;
        }
        copyStart = reqStart - lead;
        copyTotal = (int) total;
        copyCursor = 0;
        copyStarted = false;
        copyTapGeneration = tapGeneration;
        state.store ((int) ExportState::Copying, std::memory_order_release);
        s = ExportState::Copying;
    }

    if (s == ExportState::Copying)
    {
        if (cancelledGeneration.load (std::memory_order_acquire) == copyGeneration)
        {
            state.store ((int) ExportState::Idle, std::memory_order_release);
            return; // no further old-generation access after this acknowledgement
        }
        if (! tapActive || firstPos < 0 || copyTapGeneration != tapGeneration
            || copyStart < firstPos || copyStart > writeEnd)
        {
            failCopy (Failure::NotRecorded);
            return;
        }
        if (copyStart + copyTotal > writeEnd)
            return; // wait for the tail while retaining audio ownership of the header
        const auto initialMargin = copyStarted ? 0 : (std::int64_t) (0.5 * sr);
        const auto oldest = juce::jmax (firstPos, writeEnd - (std::int64_t) ringSize + initialMargin);
        if (copyStart + copyCursor < oldest)
        {
            failCopy (Failure::TooOld);
            return;
        }
        copyStarted = true;
        const auto n = juce::jmin (kCopyPerBlock, copyTotal - copyCursor);

        if (n > 0)
        {
            const auto pos = copyStart + copyCursor;
            const auto at = (int) (pos % ringSize);
            const auto first = juce::jmin (n, ringSize - at);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* src = ring.getReadPointer (ch);
                auto* dst = segment.getWritePointer (ch, copyCursor);
                juce::FloatVectorOperations::copy (dst, src + at, first);

                if (first < n)
                    juce::FloatVectorOperations::copy (dst + first, src, n - first);
            }

            copyCursor += n;

            if (n > maxCopyObserved.load (std::memory_order_relaxed))
                maxCopyObserved.store (n, std::memory_order_relaxed);
        }

        if (copyCursor >= copyTotal)
        {
           #ifdef GAPFILL_UNIT_TESTS
            if (probe != nullptr) probe (probeContext, ProbePoint::BeforePublish);
           #endif
            if (cancelledGeneration.load (std::memory_order_acquire) == copyGeneration)
            {
                state.store ((int) ExportState::Idle, std::memory_order_release);
                return;
            }
            resultSamples.store (copyTotal, std::memory_order_relaxed);
           #ifdef GAPFILL_UNIT_TESTS
            if (probe != nullptr) probe (probeContext, ProbePoint::BeforeReadyStore);
           #endif
            state.store ((int) ExportState::Ready, std::memory_order_release);
        }
    }
}

} // namespace gapfill::exporting
