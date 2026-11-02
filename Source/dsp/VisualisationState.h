#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace gapfill
{

//==============================================================================
/** One column of the scrolling visualiser. */
struct VizFrame
{
    float peak        = 0.0f;
    float conf        = 0.0f;
    float energy      = 0.0f;
    float bandConf[3] { 0.0f, 0.0f, 0.0f };
    float duck        = 1.0f;

    static constexpr std::uint8_t flagEvent       = 1 << 0;
    static constexpr std::uint8_t flagFillActive  = 1 << 1;
    static constexpr std::uint8_t flagProtected   = 1 << 2;
    static constexpr std::uint8_t flagGapEligible = 1 << 3;
    static constexpr std::uint8_t flagLookahead   = 1 << 4;
    // Reserved in Phase 0 (docs/dev/02 F0-4) so E-1 and E-3 never both edit
    // this header. flagRejected is set by E-3; flagPreview is E-1's.
    static constexpr std::uint8_t flagPreview     = 1 << 5;
    static constexpr std::uint8_t flagRejected    = 1 << 6;
    static constexpr std::uint8_t flagDiscontinuity = 1 << 7;

    std::uint8_t flags = 0;
    std::uint8_t algo  = 0xFF;   // 0xFF = no fill
    std::uint8_t band  = 0xFF;   // 0xFF = broadband
    std::uint8_t reason = 0;     // RejectReason when flagRejected is set (E-3)
    std::uint16_t availMs = 0;   // E-3: room that was available (whole ms, saturating)
    std::uint16_t needMs = 0;    // E-3: room that would have been needed (whole ms, saturating)
    std::uint32_t posLo = 0;     // output sample position at the first accumulated hop
    std::int32_t ppqTick = INT32_MIN; // PPQ * 96; invalid when the host supplied no position
    std::uint32_t periodSamples = 64; // actual producer period, preserving compact decimation
    std::uint8_t timeSigNumerator = 0, timeSigDenominator = 0; // 0 = unknown: show beats without guessed bar labels
    static constexpr std::uint8_t protectionHit = 1, protectionNext = 2, protectionLookahead = 4;
    std::uint8_t protectionKinds = 0; // classifications recorded by the engine, never inferred by the view
    std::uint32_t timelineEpoch = 0, sampleRateHz = 0;
};

static_assert (std::is_trivially_copyable_v<VizFrame>, "VizFrame travels through a lock-free FIFO");

//==============================================================================
/** Lock-free single-producer/single-consumer transfer of visualiser frames.

    The audio thread only ever writes into preallocated storage through
    juce::AbstractFifo; the editor drains it on its timer. When no editor exists
    the producer is switched off so the audio thread does no work at all. */
class VisualisationFifo
{
public:
    static constexpr int capacity = 4096;

    using ReadObserver = void (*) (void*, const VizFrame*, int);

    /** Message-thread only. Observes the existing consumer's drained frames;
        it never creates a second FIFO consumer or calls GUI code from audio. */
    void setReadObserver (void* context, ReadObserver callback) noexcept
    {
        observerContext = context;
        observer = callback;
    }

    void clearReadObserver (void* context) noexcept
    {
        if (observerContext == context)
        {
            observer = nullptr;
            observerContext = nullptr;
        }
    }

    void setEnabled (bool shouldBeEnabled) noexcept
    {
        enabled.store (shouldBeEnabled, std::memory_order_relaxed);
    }

    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    void reset() noexcept
    {
        fifo.reset();
    }

    /** Audio thread. Silently drops the frame if the consumer has stalled. */
    void push (const VizFrame& frame) noexcept
    {
        if (! isEnabled())
            return;

        const auto scope = fifo.write (1);

        if (scope.blockSize1 > 0)
            data[(size_t) scope.startIndex1] = frame;
        else if (scope.blockSize2 > 0)
            data[(size_t) scope.startIndex2] = frame;
    }

    /** GUI thread. Returns how many frames were copied into `dest`. */
    int pop (VizFrame* dest, int maxFrames) noexcept
    {
        const auto numReady = juce::jmin (maxFrames, fifo.getNumReady());

        if (numReady <= 0)
            return 0;

        const auto scope = fifo.read (numReady);

        for (int i = 0; i < scope.blockSize1; ++i)
            dest[i] = data[(size_t) (scope.startIndex1 + i)];

        for (int i = 0; i < scope.blockSize2; ++i)
            dest[scope.blockSize1 + i] = data[(size_t) (scope.startIndex2 + i)];

        const auto copied = scope.blockSize1 + scope.blockSize2;
        if (observer != nullptr)
            observer (observerContext, dest, copied);
        return copied;
    }

private:
    juce::AbstractFifo fifo { capacity };
    std::vector<VizFrame> data = std::vector<VizFrame> ((size_t) capacity);   // heap: keeps the processor small on the stack
    std::atomic<bool> enabled { false };
    void* observerContext = nullptr;
    ReadObserver observer = nullptr;
};

//==============================================================================
/** Simple atomic metering values shared with the editor. */
struct MeterState
{
    std::atomic<float> inputLevel   { 0.0f };
    std::atomic<float> outputLevel  { 0.0f };
    std::atomic<float> wetLevel     { 0.0f };
    std::atomic<float> confidence   { 0.0f };
    std::atomic<float> bandConf0    { 0.0f };
    std::atomic<float> bandConf1    { 0.0f };
    std::atomic<float> bandConf2    { 0.0f };
    std::atomic<int>   activeVoices { 0 };
    std::atomic<int>   currentAlgo  { -1 };
    std::atomic<int>   latencySamples { 0 };
    std::atomic<bool>  sidechainConnected { false };
    std::atomic<bool>  sidechainRequested { false };
    std::atomic<bool>  captureActive { false };

    // Reserved in Phase 0 (docs/dev/02 F0-4); written by the audio thread.
    static constexpr int numReasonCounters = 18;     ///< == numRejectReasons
    std::atomic<int>           previewStatus { 0 };  ///< E-1: 0 idle, otherwise a preview state code
    std::atomic<std::uint32_t> blockCounter  { 0 };  ///< blocks processed (debug overlay)
    std::atomic<std::uint32_t> reasonCount[numReasonCounters] {};   ///< gaps rejected per RejectReason
    std::atomic<bool>          syncClamped   { false };             ///< E-2: a synced length was clamped
};

} // namespace gapfill
