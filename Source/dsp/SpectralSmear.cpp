#include "SpectralSmear.h"

namespace gapfill
{

void SpectralSmear::prepare (double, int)
{
    window.resize ((size_t) fftSize);

    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = hann ((float) i / (float) fftSize);

    for (auto& c : channels)
    {
        c.fifo.assign ((size_t) fftSize, 0.0f);
        c.accum.assign ((size_t) fftSize, 0.0f);
        c.fftData.assign ((size_t) fftSize * 2, 0.0f);
        c.phase.assign ((size_t) numBins, 0.0f);
        c.smoothMag.assign ((size_t) numBins, 0.0f);
        c.writeIdx = 0;
    }

    reset();
}

void SpectralSmear::reset()
{
    for (auto& c : channels)
    {
        std::fill (c.fifo.begin(), c.fifo.end(), 0.0f);
        std::fill (c.accum.begin(), c.accum.end(), 0.0f);
        std::fill (c.phase.begin(), c.phase.end(), 0.0f);
        std::fill (c.smoothMag.begin(), c.smoothMag.end(), 0.0f);
        c.writeIdx = 0;
    }

    hopCounter = 0;
}

void SpectralSmear::setCharacter (float smear01, float jitter01, std::uint64_t seed) noexcept
{
    smear  = juce::jlimit (0.0f, 0.97f, 0.45f + 0.5f * juce::jlimit (0.0f, 1.0f, smear01));
    jitter = juce::jlimit (0.0f, 1.0f, jitter01);
    rng.seed (seed);
}

void SpectralSmear::process (float inL, float inR, float& outL, float& outR) noexcept
{
    auto& cl = channels[0];
    auto& cr = channels[1];

    const auto idx = (size_t) cl.writeIdx;

    cl.fifo[idx] = sanitise (inL);
    cr.fifo[idx] = sanitise (inR);

    outL = sanitise (cl.accum[idx]);
    outR = sanitise (cr.accum[idx]);

    cl.accum[idx] = 0.0f;
    cr.accum[idx] = 0.0f;

    cl.writeIdx = (cl.writeIdx + 1) % fftSize;
    cr.writeIdx = cl.writeIdx;

    if (++hopCounter >= hopSize)
    {
        hopCounter = 0;
        computeFrame (cl);
        computeFrame (cr);
    }
}

void SpectralSmear::computeFrame (Channel& c) noexcept
{
    auto* data = c.fftData.data();

    // The oldest sample sits at writeIdx (it is about to be overwritten).
    for (int i = 0; i < fftSize; ++i)
        data[i] = c.fifo[(size_t) ((c.writeIdx + i) % fftSize)] * window[(size_t) i];

    std::fill (c.fftData.begin() + fftSize, c.fftData.end(), 0.0f);

    fft.performRealOnlyForwardTransform (data, false);

    // Nominal phase advance per hop for each bin.
    constexpr float omega = juce::MathConstants<float>::twoPi * (float) hopSize / (float) fftSize;

    for (int k = 0; k < numBins; ++k)
    {
        const auto re = data[2 * k];
        const auto im = data[2 * k + 1];
        const auto mag = std::sqrt (re * re + im * im);

        auto& sm = c.smoothMag[(size_t) k];
        sm = smear * sm + (1.0f - smear) * mag;

        if (! std::isfinite (sm))
            sm = 0.0f;

        auto& ph = c.phase[(size_t) k];
        ph += omega * (float) k;

        if (jitter > 0.0f)
            ph += jitter * rng.nextBipolar() * 0.9f;

        // Keep the accumulator bounded so that precision never degrades.
        if (ph > juce::MathConstants<float>::twoPi * 64.0f || ph < -juce::MathConstants<float>::twoPi * 64.0f)
            ph = std::fmod (ph, juce::MathConstants<float>::twoPi);

        const auto outRe = sm * std::cos (ph);
        const auto outIm = sm * std::sin (ph);

        data[2 * k]     = outRe;
        data[2 * k + 1] = outIm;

        // Mirror into the negative-frequency half so the inverse transform sees
        // a properly conjugate-symmetric spectrum.
        if (k > 0 && k < fftSize / 2)
        {
            data[2 * (fftSize - k)]     = outRe;
            data[2 * (fftSize - k) + 1] = -outIm;
        }
    }

    data[1] = 0.0f;                       // DC imaginary part
    data[2 * (fftSize / 2) + 1] = 0.0f;   // Nyquist imaginary part

    fft.performRealOnlyInverseTransform (data);

    // Hann analysis * Hann synthesis at 75% overlap sums to 1.5.
    constexpr float normalise = 2.0f / 3.0f;

    for (int i = 0; i < fftSize; ++i)
    {
        const auto v = data[i] * window[(size_t) i] * normalise;
        auto& dst = c.accum[(size_t) ((c.writeIdx + i) % fftSize)];
        dst += std::isfinite (v) ? v : 0.0f;
    }
}

} // namespace gapfill
