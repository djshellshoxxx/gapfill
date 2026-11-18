#pragma once

#include "FormantSetup.h"
#include "StereoStft.h"

#include <array>
#include <atomic>

namespace gapfill::formant
{

/** F-L20 bounded per-voice envelope warp. All allocation is confined to prepare().
    Audio-thread ownership is exclusive from claim() to release(). Unlike the
    shipped bus card, this stage has exactly N samples of per-voice onset delay;
    it never changes plugin PDC. */
class FormantProcessor final : private StereoFrameCallback
{
public:
    static int sizeForRate (double sampleRate) noexcept
    {
        return sampleRate <= 48000.0 ? 512 : (sampleRate <= 96000.0 ? 1024 : 2048);
    }

    void prepare (double sampleRate);
    void reset() noexcept;
    bool claim (const FormantSetup&) noexcept;
    void release() noexcept { claimed = false; }
    bool isClaimed() const noexcept { return claimed; }
    int latencySamples() const noexcept { return stft.size(); }
    std::uint64_t faultCount() const noexcept { return faults.load (std::memory_order_relaxed); }
    std::size_t preallocatedBytes() const noexcept;

    /** One stereo pair in place; neutral/unclaimed is bit-identical and undelayed. */
    void process (float& left, float& right) noexcept;

private:
    void processFrame (spectral::SpectralFrame&) noexcept;
    void processFrames (spectral::SpectralFrame&, spectral::SpectralFrame&) noexcept override;
    void estimateEnvelopes (const spectral::SpectralFrame&, const spectral::SpectralFrame&) noexcept;

    StereoStft stft;
    std::unique_ptr<juce::dsp::FFT> cepstralFft;
    std::vector<float> envelopeLog, targetLog;
    std::array<std::vector<float>, 2> channelEnvelope;
    std::vector<std::complex<float>> cepInput, cepOutput;
    struct WarpBin { int index = 0; float fraction = 0.0f, tilt = 0.0f; };
    std::vector<WarpBin> warp;
    std::vector<float> octave;
    std::array<std::vector<float>, 6> vowelShapes;
    std::array<float, 6> vowelMeans {};
    std::array<std::vector<float>, 2> dryDelay;
    float wetPrevious[2] { 0.0f, 0.0f };
    int dryIndex = 0, lifter = 48;
    FormantSetup settings;
    bool claimed = false, frameFault = false;
    bool imposeVowel = false;
    std::atomic<std::uint64_t> faults { 0 };
};

} // namespace gapfill::formant
