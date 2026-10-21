#include "SpectralSmear.h"

namespace gapfill
{

void SpectralSmear::prepare (double, int)
{
    window.resize ((size_t) fftSize);

    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = hann ((float) i / (float) fftSize);

    windowLow.resize ((size_t) fftSize / 2);

    for (int i = 0; i < fftSize / 2; ++i)
        windowLow[(size_t) i] = hann ((float) i / (float) (fftSize / 2));

    lowerOrder = false;
    curSize = fftSize;
    curHop = hopSize;
    curBins = numBins;

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

void SpectralSmear::setLowerOrder (bool lower) noexcept
{
    if (lower == lowerOrder)
        return;

    lowerOrder = lower;
    curSize = lower ? fftSize / 2 : fftSize;
    curHop = curSize / 4;
    curBins = curSize / 2 + 1;
    reset();                                   // different frame length: restart from silence
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
    scramblePhases();
}

void SpectralSmear::scramblePhases() noexcept
{
    // Every bin has to start somewhere of its own. Starting them all at zero
    // leaves the bins coherent, and a spectrum whose bins all line up is an
    // impulse: because each phase then advances at exactly its nominal rate,
    // they realign every fftSize samples and the output ticks once per period
    // instead of smearing.
    for (auto& c : channels)
        for (auto& ph : c.phase)
            ph = rng.nextBipolar() * juce::MathConstants<float>::pi;
}

void SpectralSmear::setCharacter (float smear01, float jitter01, std::uint64_t seed) noexcept
{
    smear  = juce::jlimit (0.0f, 0.97f, 0.45f + 0.5f * juce::jlimit (0.0f, 1.0f, smear01));
    jitter = juce::jlimit (0.0f, 1.0f, jitter01);
    rng.seed (seed);
    scramblePhases();
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

    cl.writeIdx = (cl.writeIdx + 1) % curSize;
    cr.writeIdx = cl.writeIdx;

    if (++hopCounter >= curHop)
    {
        hopCounter = 0;
        computeFrame (cl);
        computeFrame (cr);
    }
}

void SpectralSmear::computeFrame (Channel& c) noexcept
{
    auto* data = c.fftData.data();
    const int N = curSize;
    const auto& win = lowerOrder ? windowLow : window;
    auto& transform = lowerOrder ? fftLow : fft;

    // The oldest sample sits at writeIdx (it is about to be overwritten).
    for (int i = 0; i < N; ++i)
        data[i] = c.fifo[(size_t) ((c.writeIdx + i) % N)] * win[(size_t) i];

    std::fill (c.fftData.begin() + N, c.fftData.begin() + 2 * N, 0.0f);

    transform.performRealOnlyForwardTransform (data, false);

    // Nominal phase advance per hop for each bin.
    const float omega = juce::MathConstants<float>::twoPi * (float) curHop / (float) N;

    for (int k = 0; k < curBins; ++k)
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
        if (k > 0 && k < N / 2)
        {
            data[2 * (N - k)]     = outRe;
            data[2 * (N - k) + 1] = -outIm;
        }
    }

    data[1] = 0.0f;                       // DC imaginary part
    data[2 * (N / 2) + 1] = 0.0f;         // Nyquist imaginary part

    transform.performRealOnlyInverseTransform (data);

    // Hann analysis * Hann synthesis at 75% overlap sums to 1.5.
    constexpr float normalise = 2.0f / 3.0f;

    for (int i = 0; i < N; ++i)
    {
        const auto v = data[i] * win[(size_t) i] * normalise;
        auto& dst = c.accum[(size_t) ((c.writeIdx + i) % N)];
        dst += std::isfinite (v) ? v : 0.0f;
    }
}

} // namespace gapfill
