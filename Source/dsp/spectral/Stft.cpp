#include "Stft.h"

#include <algorithm>
#include <cmath>

namespace gapfill::spectral
{

void Stft::prepare (double sampleRate, int numChannels, int fftSize)
{
    jassert (isValidSize (fftSize));
    N = isValidSize (fftSize) ? fftSize : 2048;
    channels = std::max (1, numChannels);
    sr = sampleRate;

    int order = 0;

    while ((1 << order) < N)
        ++order;

    fft = std::make_unique<juce::dsp::FFT> (order);
    window.assign ((size_t) N, 0.0f);

    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (kTwoPi * (float) i / (float) N));

    fftBuf.assign ((size_t) N * 2, 0.0f);
    mag.assign ((size_t) numBins(), 0.0f);
    phase.assign ((size_t) numBins(), 0.0f);
    inRing.assign ((size_t) channels, std::vector<float> ((size_t) N, 0.0f));
    acc.assign ((size_t) channels, std::vector<float> ((size_t) N * 2, 0.0f));

    // Overlap-add gain of squared periodic Hann at hop N/4 is exactly 1.5.
    synthGain = 1.0f / 1.5f;

    // Calibrate the inverse transform scaling (differs between FFT back-ends).
    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    fftBuf[0] = 1.0f;
    fft->performRealOnlyForwardTransform (fftBuf.data(), true);
    fft->performRealOnlyInverseTransform (fftBuf.data());
    inverseScale = std::abs (fftBuf[0]) > 1.0e-9f ? 1.0f / fftBuf[0] : 1.0f;

    reset();
}

void Stft::reset() noexcept
{
    for (auto& v : inRing)
        std::fill (v.begin(), v.end(), 0.0f);

    for (auto& v : acc)
        std::fill (v.begin(), v.end(), 0.0f);

    inCount = 0;
    frames = 0;
}

std::size_t Stft::preallocatedBytes() const noexcept
{
    const auto f = sizeof (float);
    return (window.size() + fftBuf.size() + mag.size() + phase.size()) * f
         + (std::size_t) channels * ((std::size_t) N + (std::size_t) N * 2) * f;
}

void Stft::runFrame (int ch, SpectralCallback& cb, std::int64_t frameIndex) noexcept
{
    const auto mask = (std::int64_t) (N - 1);
    const auto& ring = inRing[(size_t) ch];
    const auto oldest = inCount - N;                       // absolute index of the frame's first sample

    for (int i = 0; i < N; ++i)
    {
        const auto idx = (int) ((oldest + i) & mask);
        fftBuf[(size_t) i] = ring[(size_t) idx] * window[(size_t) i];
    }

    std::fill (fftBuf.begin() + N, fftBuf.end(), 0.0f);
    fft->performRealOnlyForwardTransform (fftBuf.data(), true);

    const auto bins = numBins();

    for (int k = 0; k < bins; ++k)
    {
        const auto re = fftBuf[(size_t) (2 * k)];
        const auto im = fftBuf[(size_t) (2 * k + 1)];
        mag[(size_t) k] = std::sqrt (re * re + im * im);
        phase[(size_t) k] = std::atan2 (im, re);
    }

    SpectralFrame f;
    f.channel = ch;
    f.numBins = bins;
    f.fftSize = N;
    f.hop = hop();
    f.sampleRate = sr;
    f.frameIndex = frameIndex;
    f.mag = mag.data();
    f.phase = phase.data();
    cb.processFrame (f);

    for (int k = 0; k < bins; ++k)
    {
        auto m = mag[(size_t) k];
        auto p = phase[(size_t) k];

        if (! std::isfinite (m) || ! std::isfinite (p))
        {
            m = 0.0f;
            p = 0.0f;
        }

        fftBuf[(size_t) (2 * k)] = m * std::cos (p);
        fftBuf[(size_t) (2 * k + 1)] = m * std::sin (p);
    }

    // DC and Nyquist are real for a real signal.
    fftBuf[1] = 0.0f;
    fftBuf[(size_t) (2 * (bins - 1) + 1)] = 0.0f;

    fft->performRealOnlyInverseTransform (fftBuf.data());

    auto& a = acc[(size_t) ch];
    const auto amask = (std::int64_t) (2 * N - 1);
    const auto g = inverseScale * synthGain;

    for (int i = 0; i < N; ++i)
    {
        const auto idx = (int) ((oldest + i) & amask);
        a[(size_t) idx] += fftBuf[(size_t) i] * window[(size_t) i] * g;
    }
}

void Stft::process (const float* const* in, float** out, int n, SpectralCallback& cb) noexcept
{
    if (N <= 0 || n <= 0)
        return;

    juce::ScopedNoDenormals noDenormals;

    const auto hopSize = hop();
    const auto mask = (std::int64_t) (N - 1);
    const auto amask = (std::int64_t) (2 * N - 1);

    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < channels; ++ch)
            inRing[(size_t) ch][(size_t) (inCount & mask)] = in[ch][i];

        ++inCount;

        if (inCount % hopSize == 0)
        {
            for (int ch = 0; ch < channels; ++ch)
                runFrame (ch, cb, frames);

            ++frames;
        }

        const auto readIndex = inCount - 1 - (N - 1);      // output sample index for this tick

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& slot = acc[(size_t) ch][(size_t) (readIndex & amask)];
            out[ch][i] = slot;
            slot = 0.0f;
        }
    }
}

//==============================================================================
int identityPhaseLock (const float* mag, const float* analysisPhase, float* synthPhase,
                       int numBins, int* peaks) noexcept
{
    int np = 0;

    for (int k = 2; k < numBins - 2; ++k)
    {
        const auto m = mag[k];

        if (m > mag[k - 1] && m > mag[k - 2] && m >= mag[k + 1] && m >= mag[k + 2])
            peaks[np++] = k;
    }

    if (np == 0)
        return 0;

    int pi = 0;

    for (int k = 0; k < numBins; ++k)
    {
        // Advance to the peak whose region contains k (boundary: midpoint of neighbouring peaks).
        while (pi + 1 < np && k > (peaks[pi] + peaks[pi + 1]) / 2)
            ++pi;

        const auto pk = peaks[pi];

        if (k != pk)
            synthPhase[k] = wrapPi (synthPhase[pk] + analysisPhase[k] - analysisPhase[pk]);
    }

    return np;
}

} // namespace gapfill::spectral
