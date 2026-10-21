#pragma once

// Small allocation-free DSP primitives shared by the WP9 bus cards. Header-only,
// no state outside the structs; allocate() / assign() are prepare-time only.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gapfill::fx::bus
{

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;

inline float flush (float x) noexcept { return std::abs (x) < 1.0e-20f ? 0.0f : x; }

/** Linear ramp to a target over a number of samples. */
struct Ramp
{
    float cur = 0.0f, target = 0.0f, inc = 0.0f;
    int left = 0;

    void snap (float v) noexcept { cur = target = v; inc = 0.0f; left = 0; }

    void setTarget (float t, int samples) noexcept
    {
        if (t == target)
            return;

        target = t;
        left = std::max (1, samples);
        inc = (t - cur) / (float) left;
    }

    float next() noexcept
    {
        if (left > 0)
        {
            cur += inc;

            if (--left == 0)
                cur = target;
        }

        return cur;
    }

    bool settled() const noexcept { return left == 0; }
};

/** One-pole high-pass based DC blocker. */
struct DcBlock
{
    float x1 = 0.0f, y1 = 0.0f;

    float process (float x, float r = 0.995f) noexcept
    {
        const auto y = x - x1 + r * y1;
        x1 = x;
        y1 = flush (y);
        return y;
    }

    void reset() noexcept { x1 = y1 = 0.0f; }
};

inline float onePoleCoeff (float fc, double sr) noexcept
{
    fc = std::min (fc, 0.45f * (float) sr);
    return 1.0f - std::exp (-kTwoPi * fc / (float) sr);
}

struct OnePole
{
    float z = 0.0f;

    float lp (float x, float a) noexcept { z += a * (x - z); z = flush (z); return z; }
    void reset() noexcept { z = 0.0f; }
};

/** Zero-delay-feedback state variable filter (Zavalishin TPT), 12 dB/oct. */
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float g = 0.1f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

    void set (float fc, float q, double sr) noexcept
    {
        fc = std::clamp (fc, 5.0f, 0.45f * (float) sr);
        g = std::tan (kPi * fc / (float) sr);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void process (float v0, float& lowpass, float& highpass) noexcept
    {
        const auto v3 = v0 - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flush (2.0f * v1 - ic1);
        ic2 = flush (2.0f * v2 - ic2);
        lowpass = v2;
        highpass = v0 - k * v1 - v2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }
};

/** Power-of-two circular delay line with 3rd-order Lagrange interpolation.
    read (d) returns the sample pushed d pushes ago (d = 1 is the newest); d must be >= 2. */
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0;
    int w = 0;

    void allocate (int minSamples)
    {
        int n = 8;

        while (n < minSamples + 8)
            n <<= 1;

        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }

    void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    int capacity() const noexcept { return mask + 1; }

    void push (float x) noexcept
    {
        buf[(size_t) w] = x;
        w = (w + 1) & mask;
    }

    float readInt (int d) const noexcept { return buf[(size_t) ((w - d) & mask)]; }

    float read (float d) const noexcept
    {
        d = std::clamp (d, 2.0f, (float) (mask - 3));
        const auto n = (int) d;
        const auto f = d - (float) n;
        const auto ym1 = readInt (n - 1), y0 = readInt (n), y1 = readInt (n + 1), y2 = readInt (n + 2);
        const auto cm1 = -f * (f - 1.0f) * (f - 2.0f) * (1.0f / 6.0f);
        const auto c0 = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) * 0.5f;
        const auto c1 = -(f + 1.0f) * f * (f - 2.0f) * 0.5f;
        const auto c2 = (f + 1.0f) * f * (f - 1.0f) * (1.0f / 6.0f);
        return cm1 * ym1 + c0 * y0 + c1 * y1 + c2 * y2;
    }
};

/** Phase delay (samples) of a one-pole low-pass (coefficient a) at normalised
    angular frequency w, and of the 0.995 DC blocker (negative = leads). */
inline float onePolePhaseDelay (float a, float w) noexcept
{
    const auto b = 1.0f - a;
    return std::atan2 (b * std::sin (w), 1.0f - b * std::cos (w)) / w;
}

inline float dcBlockPhaseDelay (float r, float w) noexcept
{
    // H = (1 - z^-1) / (1 - r z^-1); phase = arg(1 - e^-jw) - arg(1 - r e^-jw)
    const auto num = std::atan2 (std::sin (w), 1.0f - std::cos (w));
    const auto den = std::atan2 (r * std::sin (w), 1.0f - r * std::cos (w));
    return -(num - den) / w;
}

/** Length of one tempo division in samples (kFallbackBpm when no transport). */
inline double divisionSamples (double beats, double samplesPerBeat, double sampleRate, double fallbackBpm) noexcept
{
    const auto spb = samplesPerBeat > 1.0 ? samplesPerBeat : 60.0 / fallbackBpm * sampleRate;
    return beats * spb;
}

/** Bounded soft clamp, ~identity for small signals. */
inline float softClamp (float x, float limit = 1.5f) noexcept { return std::tanh (x / limit) * limit; }

} // namespace gapfill::fx::bus
