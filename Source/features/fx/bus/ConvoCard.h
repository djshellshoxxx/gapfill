#pragma once

// F-L21 Transient-derived convolution (docs/dev/12 section 7, BusFx).
//
// Amount = wet blend (out = in + amount x convolved). The impulse response is a
// snapshot of the strongest event of the last 4 s of the dry source history:
// segment [event - 2 ms, event + cvSize], half-Hann fade-out, high-pass 80 Hz,
// RMS-normalised to -24 dBFS, cvDamp shaping an exponentially falling high-
// frequency damping into the IR. The audio thread writes the snapshot into a
// preallocated single-slot mailbox (single producer / single consumer); a small
// worker thread (never the audio thread) loads it with
// juce::dsp::Convolution::loadImpulseResponse, which JUCE crossfades. The
// convolution uses NonUniform{256} so it reports zero latency. IR audio is never
// part of the project state: after a reload there is no IR and the card passes
// dry. A WAV IR (<= 1 s, <= 1 MB) can be loaded and the current one saved.
// Disabled in Lite. Tail cap 1.5 s.

#include "BusCardBase.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <memory>
#include <thread>

namespace gapfill::fx::bus
{

class ConvoCard final : public BusCardBase
{
public:
    static constexpr float kMaxIrSeconds = 0.5f;
    static constexpr float kIrTargetDb = -24.0f;

    ConvoCard();
    ~ConvoCard() override;

    CardId id() const noexcept override { return CardId::Convo; }
    int tailCapMs() const noexcept override { return 1500; }
    bool enabled (const FxSettings& s) const noexcept override { return ! s.lite && s.convo.amount01 > kOffThreshold; }

    // ---- control (any thread) ------------------------------------------------
    /** Capture the strongest event of the last 4 s as the new IR ("Capture IR from last hit"). */
    void requestCapture() noexcept { captureRequests.fetch_add (1, std::memory_order_acq_rel); }
    void setAutoCapture (bool on) noexcept { autoCapture.store (on, std::memory_order_relaxed); }
    bool getAutoCapture() const noexcept { return autoCapture.load (std::memory_order_relaxed); }

    bool hasIr() const noexcept { return irLoaded.load (std::memory_order_acquire); }
    std::uint32_t irSequence() const noexcept { return irSeq.load (std::memory_order_acquire); }
    int irLengthSamples() const noexcept { return irLength.load (std::memory_order_acquire); }

    /** Message thread. Validates (WAV, <= 1 s, <= 1 MB, finite) and loads. */
    bool loadIrFromFile (const juce::File&);
    /** Message thread. Writes the current IR as a 32-bit float WAV. */
    bool saveIrToFile (const juce::File&) const;
    /** Any thread; non-audio. Validates and queues an IR (size / NaN checks). Returns false when rejected. */
    bool loadIrFromBuffer (const juce::AudioBuffer<float>& buffer, double bufferSampleRate);
    /** Clears the IR (card passes dry again). */
    void clearIr();

    // ---- pure helpers (tests) -------------------------------------------------
    /** Builds the IR from a source segment (see class comment). `out` has capacity for
        `lengthSamples`; returns false when the segment is silent or not finite. */
    static bool shapeIr (juce::AudioBuffer<float>& ir, int lengthSamples, double sr, float damp01) noexcept;

    /** Index of the strongest event in a mono signal: the sample of the largest absolute
        value, moved back to the earliest sample within `onsetWindow` before it that
        still exceeds a quarter of that peak. Returns -1 when the signal is silent. */
    static int findStrongestEvent (const float* mono, int n, int onsetWindow) noexcept;

    /** Latest Convolution counters for tests. */
    std::uint32_t capturesDone() const noexcept { return capturesTaken.load (std::memory_order_relaxed); }
    std::uint32_t rejected() const noexcept { return rejectedCount.load (std::memory_order_relaxed); }

protected:
    float amountOf (const FxSettings& s) const noexcept override { return s.convo.amount01; }
    void prepareCard() override;
    void resetCard() noexcept override;
    void blockSettings (const FxSettings&, const BusFxContext&) noexcept override;
    void renderChunk (float*, float*, int, const float*, const float*, const BusFxContext&, const FxSettings&) noexcept override;

private:
    enum Slot : int { Empty = 0, Writing, Ready };

    void captureNow (const BusFxContext&, const ConvoSettings&) noexcept;
    void workerLoop();
    void queueIr (juce::AudioBuffer<float>&& ir, double irRate);

    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 256 } };
    juce::AudioBuffer<float> scratch, capScratch;
    std::vector<float> monoScratch;
    std::int64_t lastScanPos = 0;
    juce::dsp::ProcessSpec spec {};

    std::atomic<std::uint32_t> captureRequests { 0 };
    std::uint32_t captureHandled = 0;
    std::atomic<bool> autoCapture { false };
    std::int64_t pendingCaptureAt = -1;        ///< history position at which the snapshot is taken
    float lastSize = 120.0f, lastDamp = 0.4f;

    // audio thread -> worker mailbox
    juce::AudioBuffer<float> mailbox;
    int mailboxLength = 0;
    std::atomic<int> slot { Empty };

    std::atomic<bool> irLoaded { false };
    std::atomic<std::uint32_t> irSeq { 0 };
    std::atomic<int> irLength { 0 };
    std::atomic<std::uint32_t> capturesTaken { 0 }, rejectedCount { 0 };

    // worker
    std::thread worker;
    std::atomic<bool> stopWorker { false };
    mutable juce::CriticalSection irLock;      ///< guards currentIr (worker / message thread only)
    juce::AudioBuffer<float> currentIr;
    double currentIrRate = 48000.0;
    bool prepared = false;
};

} // namespace gapfill::fx::bus
