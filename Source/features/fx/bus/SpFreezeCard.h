#pragma once

// F-X02 Spectral freeze and morph (docs/dev/12 section 7, BusFx, SP-0).
//
// Amount = wet blend (out = in + amount x frozen sound). A capture (command, or
// automatically at each natural fill start while Auto-capture is on, default on)
// averages the magnitude of the last 4 STFT frames of the dry SourceHistory window at
// the anchor into preallocated frozen frames (4 FFTs per channel, bounded) and keeps
// the last frame's phase and the instantaneous-frequency deviation. Synthesis runs
// on the SP-0 STFT and sustains that magnitude with propagated phases (initialised
// by the deterministic sub-RNG kSalt_SpFreeze, then identity-phase-locked around the
// spectral peaks) for sfzLength ms with an exponential decay (RT60 0.1-4 s).
// Smear = +-k-bin box blur (k <= 16 x smear), pitch = bin remap by 2^(sfzPitch/12)
// (interpolated), width = decorrelation of the R phases, morph = linear interpolation
// of magnitudes between frame A and frame B (B = second capture, or the live source
// when there is no second capture). Release and Panic fade over 20 ms.
// Capture / release are atomic request counters (any thread); the UI only sees the
// sequence counters, never the buffers. Disabled in Lite. The synthesis lags by the
// SP-0 latency; the card applies the engine duck so the next hit stays protected.

#include "BusCardBase.h"
#include "../../../dsp/spectral/Stft.h"

namespace gapfill::fx::bus
{

class SpFreezeCard final : public BusCardBase, private spectral::SpectralCallback
{
public:
    CardId id() const noexcept override { return CardId::SpFreeze; }
    int latencySamples() const noexcept override { return stft.latencySamples(); }
    int tailCapMs() const noexcept override { return 4500; }
    bool enabled (const FxSettings& s) const noexcept override { return ! s.lite && s.spfreeze.amount01 > kOffThreshold; }
    bool isIdle() const noexcept override { return BusCardBase::isIdle() && ! sounding.load (std::memory_order_acquire); }

    // ---- control (any thread) -------------------------------------------------
    void requestCapture() noexcept { captureReq.fetch_add (1, std::memory_order_acq_rel); }
    void requestRelease() noexcept { releaseReq.fetch_add (1, std::memory_order_acq_rel); }
    void setAutoCapture (bool on) noexcept { autoCapture.store (on, std::memory_order_relaxed); }
    bool getAutoCapture() const noexcept { return autoCapture.load (std::memory_order_relaxed); }

    std::uint32_t captureSequence() const noexcept { return captureSeq.load (std::memory_order_acquire); }
    bool isSounding() const noexcept { return sounding.load (std::memory_order_acquire); }
    bool hasSecondCapture() const noexcept { return haveB.load (std::memory_order_acquire); }

    std::size_t preallocatedBytes() const noexcept;
    int fftSize() const noexcept { return stft.size(); }

    /** Test access: the frozen magnitude of frame A / B (channel 0) after the last capture. */
    const std::vector<float>& frozenMagnitude (int frame) const noexcept { return frames[frame & 1].mag[0]; }

protected:
    float amountOf (const FxSettings& s) const noexcept override { return s.spfreeze.amount01; }
    void prepareCard() override;
    void resetCard() noexcept override;
    void blockSettings (const FxSettings&, const BusFxContext&) noexcept override;
    void renderChunk (float*, float*, int, const float*, const float*, const BusFxContext&, const FxSettings&) noexcept override;

private:
    struct Frame
    {
        std::vector<float> mag[2], phase[2], dev[2];
    };

    void processFrame (spectral::SpectralFrame&) noexcept override;
    void captureInto (Frame&, const BusFxContext&, std::int64_t anchor) noexcept;
    void trigger (const BusFxContext&, std::int64_t anchor, std::uint32_t seed) noexcept;
    void startSustain (std::uint32_t seed) noexcept;

    spectral::Stft stft;
    std::unique_ptr<juce::dsp::FFT> cfft;
    std::vector<float> window, fftBuf, accMag, mixMag, mixDev, outMag, prefix, widthOff, refPhase;
    std::vector<int> peaks;
    std::vector<float> synth[2];
    juce::AudioBuffer<float> capBuf;
    Frame frames[2];
    bool haveA = false;
    std::atomic<bool> haveB { false };

    std::atomic<std::uint32_t> captureReq { 0 }, releaseReq { 0 }, captureSeq { 0 };
    std::uint32_t captureHandled = 0, releaseHandled = 0;
    std::atomic<bool> autoCapture { true };
    std::atomic<bool> sounding { false };

    // sustain state (audio thread)
    bool active = false;
    std::int64_t elapsed = 0;
    std::int64_t lengthSamples = 0;
    float level = 1.0f, decayPerSample = 1.0f;
    int releaseLeft = 0, releaseTotal = 1;
    bool releasing = false;
    int fadeInLeft = 0;
    float morph = 0.0f, smear = 0.0f, pitchRatio = 1.0f, width = 0.5f;

    float outL[BusCardBase::kChunk] {}, outR[BusCardBase::kChunk] {};
};

} // namespace gapfill::fx::bus
