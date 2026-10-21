#include "ExportService.h"

namespace gapfill::exporting
{

void ExportService::prepare (double sampleRate)
{
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
    if (ringSize == 0)
        return RequestResult::NotPrepared;

    if (! span.isKnown())
        return RequestResult::NoSpan;

    // Only the message thread leaves Idle, so the fields can be written while the state is Idle.
    if (state.load (std::memory_order_acquire) != (int) ExportState::Idle)
        return RequestResult::Busy;

    reqStart = span.startPos;
    reqLength = span.lengthSamples;

    int expected = (int) ExportState::Idle;

    return state.compare_exchange_strong (expected, (int) ExportState::Requested, std::memory_order_release)
               ? RequestResult::Accepted : RequestResult::Busy;
}

bool ExportService::cancelPending() noexcept
{
    int expected = (int) ExportState::Requested;
    return state.compare_exchange_strong (expected, (int) ExportState::Idle, std::memory_order_acq_rel);
}

bool ExportService::take (juce::AudioBuffer<float>& out, Failure& why)
{
    why = Failure::None;
    const auto s = (ExportState) state.load (std::memory_order_acquire);

    if (s == ExportState::Ready)
    {
        const auto n = resultSamples.load (std::memory_order_relaxed);
        out.setSize (2, n, false, false, true);

        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, 0, segment, ch, 0, n);

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

    if (isArmed && numSamples > 0 && numSamples <= ringSize)
    {
        if (! tapActive)
        {
            tapActive = true;
            firstPos = -1;
        }

        // A new continuous run starts at arming and after any jump of the output timeline.
        if (firstPos < 0 || outputPos != writeEnd)
            firstPos = outputPos;

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
    else if (! isArmed)
    {
        tapActive = false;
    }

    // ---- the mailbox ---------------------------------------------------------
    auto s = (ExportState) state.load (std::memory_order_acquire);

    if (s == ExportState::Requested)
    {
        const auto lead = (std::int64_t) std::llround (kLeadInSeconds * sr);
        const auto tail = (std::int64_t) std::llround (kTailSeconds * sr);
        const auto start = reqStart - lead;
        const auto end = reqStart + (std::int64_t) reqLength + tail;
        const auto total = end - start;

        auto fail = [this] (Failure why)
        {
            failure.store ((int) why, std::memory_order_relaxed);
            state.store ((int) ExportState::Failed, std::memory_order_release);
        };

        // A copy takes several blocks while the ring keeps moving: keep half a second of margin.
        const auto oldestSafe = writeEnd - (std::int64_t) ringSize + (std::int64_t) (0.5 * sr);

        if (total > (std::int64_t) segmentCapacity)
            fail (Failure::TooLong);
        else if (! tapActive || firstPos < 0 || start < firstPos || start > writeEnd)
            fail (Failure::NotRecorded);            // not captured (or from before a timeline reset)
        else if (start < oldestSafe)
            fail (Failure::TooOld);
        else if (end <= writeEnd)
        {
            copyStart = start;
            copyTotal = (int) total;
            copyCursor = 0;
            state.store ((int) ExportState::Copying, std::memory_order_release);
            s = ExportState::Copying;
        }
        // else: the fill is still playing; wait for its end to be written (the editor times out).
    }

    if (s == ExportState::Copying)
    {
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
            resultSamples.store (copyTotal, std::memory_order_relaxed);
            state.store ((int) ExportState::Ready, std::memory_order_release);
        }
    }
}

} // namespace gapfill::exporting
