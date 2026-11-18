#include "StereoStft.h"

#include <algorithm>
#include <cmath>

namespace gapfill::formant
{

void StereoStft::prepare (double sampleRate, int size)
{
    n = size;
    sr = sampleRate;
    int order = 0;
    while ((1 << order) < n) ++order;
    fft = std::make_unique<juce::dsp::FFT> (order);
    window.resize ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        window[(std::size_t) i] = 0.5f - 0.5f * std::cos (spectral::kTwoPi * (float) i / (float) n);
    for (int channel = 0; channel < 2; ++channel)
    {
        ring[(std::size_t) channel].assign ((std::size_t) n, 0.0f);
        accum[(std::size_t) channel].assign ((std::size_t) n * 2, 0.0f);
        magnitude[(std::size_t) channel].resize ((std::size_t) numBins());
        original[(std::size_t) channel].resize ((std::size_t) numBins());
        spectrum[(std::size_t) channel].resize ((std::size_t) numBins());
    }
    time.assign ((std::size_t) n, {});
    frequency.assign ((std::size_t) n, {});
    inverse.assign ((std::size_t) n, {});
    time[0] = { 1.0f, 0.0f };
    fft->perform (time.data(), frequency.data(), false);
    fft->perform (frequency.data(), inverse.data(), true);
    inverseScale = std::abs (inverse[0].real()) > 1.0e-9f ? 1.0f / inverse[0].real() : 1.0f;
    reset();
}

void StereoStft::reset() noexcept
{
    for (auto& values : ring) std::fill (values.begin(), values.end(), 0.0f);
    for (auto& values : accum) std::fill (values.begin(), values.end(), 0.0f);
    consumed = frameIndex = 0;
}

std::size_t StereoStft::preallocatedBytes() const noexcept
{
    return sizeof (float) * (window.size() + (std::size_t) 2 * (n * 3 + numBins() * 2))
        + sizeof (Complex) * (time.size() + frequency.size() + inverse.size() + (std::size_t) numBins() * 2);
}

void StereoStft::frame (StereoFrameCallback& callback) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    const auto mask = (std::int64_t) (n - 1), oldest = consumed - n;
    for (int i = 0; i < n; ++i)
    {
        const auto at = (std::size_t) ((oldest + i) & mask);
        time[(std::size_t) i] = Complex (ring[0][at], ring[1][at]) * window[(std::size_t) i];
    }
    fft->perform (time.data(), frequency.data(), false);
    for (int k = 0; k < numBins(); ++k)
    {
        const Complex reflected = std::conj (frequency[(std::size_t) ((n - k) & (n - 1))]);
        spectrum[0][(std::size_t) k] = (frequency[(std::size_t) k] + reflected) * 0.5f;
        const auto difference = frequency[(std::size_t) k] - reflected;
        spectrum[1][(std::size_t) k] = { difference.imag() * 0.5f, -difference.real() * 0.5f };
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto valueAtBin = spectrum[(std::size_t) channel][(std::size_t) k];
            const float value = std::sqrt (valueAtBin.real() * valueAtBin.real() + valueAtBin.imag() * valueAtBin.imag());
            magnitude[(std::size_t) channel][(std::size_t) k] = value;
            original[(std::size_t) channel][(std::size_t) k] = value;
        }
    }
    spectral::SpectralFrame frames[2];
    for (int channel = 0; channel < 2; ++channel)
    {
        auto& f = frames[channel];
        f.channel = channel; f.numBins = numBins(); f.fftSize = n; f.hop = n / 4;
        f.sampleRate = sr; f.frameIndex = frameIndex;
        f.mag = magnitude[(std::size_t) channel].data(); f.phase = nullptr;
    }
    callback.processFrames (frames[0], frames[1]);
    for (int k = 0; k < numBins(); ++k)
    {
        Complex scaled[2];
        for (int channel = 0; channel < 2; ++channel)
        {
            const float before = original[(std::size_t) channel][(std::size_t) k];
            const float after = magnitude[(std::size_t) channel][(std::size_t) k];
            const float gain = before > 1.0e-30f ? after / before : 0.0f;
            scaled[channel] = std::isfinite (gain) && std::isfinite (before) && gain >= 0.0f
                ? spectrum[(std::size_t) channel][(std::size_t) k] * gain : Complex {};
        }
        frequency[(std::size_t) k] = scaled[0] + Complex (-scaled[1].imag(), scaled[1].real());
        if (k > 0 && k < n / 2)
            frequency[(std::size_t) (n - k)] = std::conj (scaled[0]) + Complex (scaled[1].imag(), scaled[1].real());
    }
    fft->perform (frequency.data(), inverse.data(), true);
    const auto outputMask = (std::int64_t) (n * 2 - 1);
    for (int i = 0; i < n; ++i)
    {
        const auto at = (std::size_t) ((oldest + i) & outputMask);
        const auto value = inverse[(std::size_t) i] * (window[(std::size_t) i] * inverseScale * (2.0f / 3.0f));
        accum[0][at] += std::isfinite (value.real()) ? value.real() : 0.0f;
        accum[1][at] += std::isfinite (value.imag()) ? value.imag() : 0.0f;
    }
    ++frameIndex;
}

void StereoStft::process (const float* const* input, float** output, int count, StereoFrameCallback& callback) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        const auto at = (std::size_t) (consumed & (n - 1));
        for (int channel = 0; channel < 2; ++channel) ring[(std::size_t) channel][at] = input[channel][i];
        if (++consumed % (n / 4) == 0) frame (callback);
        const auto read = (std::size_t) ((consumed - n) & (n * 2 - 1));
        for (int channel = 0; channel < 2; ++channel)
        {
            auto& value = accum[(std::size_t) channel][read];
            output[channel][i] = value;
            value = 0.0f;
        }
    }
}

} // namespace gapfill::formant
