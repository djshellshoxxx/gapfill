#include "ChromaTracker.h"

#include <algorithm>
#include <cmath>

namespace gapfill::keydetect
{

namespace
{
    constexpr float kLowHz = 65.0f, kHighHz = 2000.0f;
    constexpr int   kLocalHalfWidth = 25;                  // 51-bin running mean approximates the local median
    constexpr float kPeakOverLocal = 3.0f;
    constexpr float kTonalnessGate = 0.35f;
    constexpr float kEnergyGateMeanSquare = 3.1622777e-6f;  // -55 dBFS
    constexpr float kFloor = 1.0e-12f;

    void butterworthLowpass (float fc, float fs, float q, float (&b)[3], float (&a)[2])
    {
        const float w0 = 6.2831853f * fc / fs;
        const float cosw = std::cos (w0), alpha = std::sin (w0) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b[0] = (1.0f - cosw) * 0.5f / a0;
        b[1] = (1.0f - cosw) / a0;
        b[2] = b[0];
        a[0] = -2.0f * cosw / a0;
        a[1] = (1.0f - alpha) / a0;
    }
}

void ChromaTracker::prepare (double sampleRate)
{
    sr = sampleRate > 8000.0 ? sampleRate : 48000.0;
    decim = 1 << std::max (0, (int) std::lround (std::log2 (sr / 12000.0)));
    const double fs = sr / (double) decim;
    hopSec = (double) kHop / fs;

    const float cutoff = std::min (5000.0f, 0.42f * (float) fs);
    float b[3], a[2];

    for (int s = 0; s < 2; ++s)
    {
        butterworthLowpass (cutoff, (float) sr, s == 0 ? 0.5411961f : 1.3065630f, b, a);
        lp[s] = {};
        lp[s].b0 = b[0]; lp[s].b1 = b[1]; lp[s].b2 = b[2]; lp[s].a1 = a[0]; lp[s].a2 = a[1];
    }

    ring.assign ((size_t) kFftSize, 0.0f);
    fft = std::make_unique<juce::dsp::FFT> (kFftOrder);
    fftData.assign ((size_t) kFftSize * 2, 0.0f);
    window.resize ((size_t) kFftSize);

    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) kFftSize);

    mag.assign ((size_t) kBins, 0.0f);
    prefix.assign ((size_t) kBins + 1, 0.0);
    setWindowSeconds (windowSec);
    reset();
}

void ChromaTracker::setWindowSeconds (float seconds) noexcept
{
    windowSec = std::clamp (seconds, 4.0f, 30.0f);
    lambda = (float) std::exp (-hopSec / (double) windowSec);
}

void ChromaTracker::reset() noexcept
{
    for (auto& l : lp)
        l.z1 = l.z2 = 0.0f;

    decimCounter = 0;
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringWrite = sinceFrame = filled = 0;

    for (auto& c : accum)
        c = 0.0f;

    mass = 0.0f;
    tuning = 0.0f;
    frames = 0;
    lastGate = false;
}

int ChromaTracker::process (const float* mono, int numSamples) noexcept
{
    int completed = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float y = lp[1].process (lp[0].process (mono[i]));

        if (++decimCounter < decim)
            continue;

        decimCounter = 0;
        ring[(size_t) ringWrite] = y;
        ringWrite = (ringWrite + 1) % kFftSize;
        filled = std::min (filled + 1, kFftSize);

        if (++sinceFrame >= kHop)
        {
            sinceFrame = 0;

            if (filled >= kFftSize)
            {
                analyseFrame();
                ++completed;
            }
        }
    }

    return completed;
}

void ChromaTracker::analyseFrame() noexcept
{
    // ---- windowed copy of the newest kFftSize samples -------------------------------
    double energy = 0.0;

    for (int i = 0; i < kFftSize; ++i)
    {
        const float x = ring[(size_t) ((ringWrite + i) % kFftSize)];
        energy += (double) x * (double) x;
        fftData[(size_t) i] = x * window[(size_t) i];
    }

    std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);
    const bool loudEnough = (float) (energy / kFftSize) > kEnergyGateMeanSquare;

    fft->performRealOnlyForwardTransform (fftData.data(), true);

    for (int k = 0; k < kBins; ++k)
    {
        const float re = fftData[(size_t) (2 * k)], im = fftData[(size_t) (2 * k + 1)];
        mag[(size_t) k] = std::sqrt (re * re + im * im);
    }

    // ---- peaks -------------------------------------------------------------------
    const double fs = sr / (double) decim;
    const double binHz = fs / (double) kFftSize;
    const int kLo = std::max (2, (int) std::ceil ((double) kLowHz / binHz));
    const int kHi = std::min (kBins - 3, (int) std::floor ((double) kHighHz / binHz));

    prefix[0] = 0.0;

    for (int k = 0; k < kBins; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + (double) mag[(size_t) k];

    double totalEnergy = 0.0, peakEnergy = 0.0;
    float chroma[12] {};
    float chromaSum = 0.0f;
    double residualSum = 0.0, residualWeight = 0.0;

    for (int k = kLo; k <= kHi; ++k)
        totalEnergy += (double) mag[(size_t) k] * (double) mag[(size_t) k];

    for (int k = kLo; k <= kHi; ++k)
    {
        const float b = mag[(size_t) k];

        if (b <= mag[(size_t) k - 1] || b < mag[(size_t) k + 1])
            continue;

        const int lo = std::max (0, k - kLocalHalfWidth), hi = std::min (kBins - 1, k + kLocalHalfWidth);
        const double local = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (double) (hi - lo + 1);

        if (b <= kPeakOverLocal * (float) local)
            continue;

        peakEnergy += (double) b * (double) b + (double) mag[(size_t) k - 1] * (double) mag[(size_t) k - 1]
                    + (double) mag[(size_t) k + 1] * (double) mag[(size_t) k + 1];

        const float a = mag[(size_t) k - 1], c = mag[(size_t) k + 1];
        const float den = a - 2.0f * b + c;
        const float delta = std::abs (den) > 1.0e-12f ? std::clamp (0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;
        const double f = ((double) k + (double) delta) * binHz;

        if (f < (double) kLowHz || f > (double) kHighHz)
            continue;

        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        const double corrected = midi - (double) tuning / 100.0;
        const long nearest = std::lround (corrected);
        const int pc = (int) (((nearest % 12) + 12) % 12);
        const float w = std::pow (b, 0.7f);

        chroma[pc] += w;
        chromaSum += w;
        residualSum += (midi - (double) std::lround (midi)) * 100.0 * (double) w;
        residualWeight += (double) w;
    }

    const double tonalness = totalEnergy > 1.0e-18 ? std::min (1.0, peakEnergy / totalEnergy) : 0.0;
    const bool gate = loudEnough && tonalness >= (double) kTonalnessGate && chromaSum > 0.0f;
    lastGate = gate;

    // ---- leaky accumulation ----------------------------------------------------------
    for (int pc = 0; pc < 12; ++pc)
    {
        const float v = gate ? chroma[pc] / chromaSum : 0.0f;
        accum[pc] = std::max (kFloor, lambda * accum[pc] + (1.0f - lambda) * v);
    }

    mass = std::max (0.0f, lambda * mass + (1.0f - lambda) * (gate ? 1.0f : 0.0f));

    if (gate && residualWeight > 0.0)
    {
        const float measured = (float) (residualSum / residualWeight);
        tuning = std::clamp (tuning + 0.1f * (measured - tuning), -50.0f, 50.0f);
    }

    ++frames;
}

} // namespace gapfill::keydetect
