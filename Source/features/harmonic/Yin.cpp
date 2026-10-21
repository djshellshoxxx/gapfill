#include "Yin.h"

#include <algorithm>
#include <cmath>

namespace gapfill::harmonic
{

int yinDecimation (double sampleRate) noexcept
{
    return std::max (1, (int) std::lround (sampleRate / 12000.0));
}

int yinSourceSamples (double sampleRate) noexcept
{
    return kYinWindow * yinDecimation (sampleRate);
}

YinResult yinDetect (const float* mono, int numSamples, double sampleRate) noexcept
{
    const int decim = yinDecimation (sampleRate);

    if (mono == nullptr || numSamples < kYinWindow * decim || sampleRate < 8000.0)
        return {};

    // Decimate by averaging (a box low-pass is enough for f0 below 1 kHz).
    float x[kYinWindow];
    const float* base = mono + (numSamples - kYinWindow * decim);

    for (int i = 0; i < kYinWindow; ++i)
    {
        float acc = 0.0f;

        for (int k = 0; k < decim; ++k)
            acc += base[i * decim + k];

        x[i] = acc / (float) decim;
    }

    return yinDetectDecimated (x, sampleRate / (double) decim);
}

YinResult yinDetectDecimated (const float* x, double fs) noexcept
{
    YinResult out;

    if (x == nullptr || fs < 4000.0)
        return out;

    const int tauMin = std::max (2, (int) std::floor (fs / 1100.0));
    const int tauMax = std::min (kYinMaxTau, (int) std::floor (fs / 30.0));
    const int W = kYinWindow - tauMax;

    if (W < 64 || tauMax <= tauMin + 2)
        return out;

    // ---- difference function and cumulative mean normalisation ---------------------------------
    float dPrime[kYinMaxTau + 2];
    dPrime[0] = 1.0f;
    double running = 0.0;

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double d = 0.0;

        for (int j = 0; j < W; ++j)
        {
            const double diff = (double) x[j] - (double) x[j + tau];
            d += diff * diff;
        }

        running += d;
        dPrime[tau] = running > 1.0e-12 ? (float) (d * tau / running) : 1.0f;
    }

    // ---- absolute threshold: first dip below it, walked down to its local minimum ---------------
    int best = -1;

    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (dPrime[tau] < kYinThreshold)
        {
            while (tau + 1 <= tauMax && dPrime[tau + 1] < dPrime[tau])
                ++tau;

            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        // No dip below the threshold: the global minimum, reported with its (poor) confidence.
        float lowest = 2.0f;

        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (dPrime[tau] < lowest)
            {
                lowest = dPrime[tau];
                best = tau;
            }

        if (best < 0)
            return out;
    }

    // ---- parabolic interpolation around the minimum --------------------------------------------
    double tauExact = (double) best;

    if (best > tauMin && best < tauMax)
    {
        const double a = dPrime[best - 1], b = dPrime[best], c = dPrime[best + 1];
        const double den = a - 2.0 * b + c;

        if (std::abs (den) > 1.0e-12)
            tauExact += std::clamp (0.5 * (a - c) / den, -1.0, 1.0);
    }

    out.f0Hz = (float) (fs / tauExact);
    out.confidence = std::clamp (1.0f - dPrime[best], 0.0f, 1.0f);
    out.voiced = dPrime[best] < kYinThreshold + 0.05f && out.confidence >= kVoicedConfidence
              && out.f0Hz >= 30.0f && out.f0Hz <= 1100.0f;

    // Stationarity gate: a decaying drum hit (a 55 Hz kick is a clean sine) or a rising onset is not a
    // note to harmonise. The first and last third of the window must be within 6 dB of each other.
    if (out.voiced)
    {
        constexpr int third = kYinWindow / 3;
        double eFirst = 0.0, eLast = 0.0;

        for (int i = 0; i < third; ++i)
        {
            eFirst += (double) x[i] * (double) x[i];
            eLast += (double) x[kYinWindow - 1 - i] * (double) x[kYinWindow - 1 - i];
        }

        const double hi = std::max (eFirst, eLast), lo = std::min (eFirst, eLast);

        if (hi > 4.0 * lo)
            out.voiced = false;
    }

    return out;
}

} // namespace gapfill::harmonic
