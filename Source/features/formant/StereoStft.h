#pragma once

// Two real channels share one complex FFT. The channels retain independent
// spectra/envelopes; packing is a lossless algebraic transform, not a mono link.
#include "../../dsp/spectral/Stft.h"

#include <array>
#include <complex>

namespace gapfill::formant
{

class StereoFrameCallback
{
public:
    virtual ~StereoFrameCallback() = default;
    virtual void processFrames (spectral::SpectralFrame&, spectral::SpectralFrame&) noexcept = 0;
};

class StereoStft
{
public:
    void prepare (double sampleRate, int size);
    void reset() noexcept;
    int size() const noexcept { return n; }
    int numBins() const noexcept { return n / 2 + 1; }
    double getSampleRate() const noexcept { return sr; }
    bool isPrepared() const noexcept { return n > 0; }
    std::size_t preallocatedBytes() const noexcept;
    void process (const float* const*, float**, int count, StereoFrameCallback&) noexcept;

private:
    void frame (StereoFrameCallback&) noexcept;
    using Complex = std::complex<float>;
    int n = 0;
    double sr = 48000.0;
    std::int64_t consumed = 0, frameIndex = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    std::array<std::vector<float>, 2> ring, accum, magnitude, original;
    std::array<std::vector<Complex>, 2> spectrum;
    std::vector<Complex> time, frequency, inverse;
    float inverseScale = 1.0f;
};

} // namespace gapfill::formant
