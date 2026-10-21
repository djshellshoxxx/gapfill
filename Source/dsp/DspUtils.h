#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <cstdint>

namespace gapfill
{

//==============================================================================
// Small numeric helpers
//==============================================================================

inline constexpr float kTinyLevel = 1.0e-9f;

/** Replaces NaN/Inf with a safe value and hard-limits the magnitude.
    Every value leaving a generator passes through here so that a pathological
    parameter combination can never poison the host's audio stream. */
inline float sanitise (float x, float limit = 8.0f) noexcept
{
    if (! std::isfinite (x))
        return 0.0f;

    return juce::jlimit (-limit, limit, x);
}

inline float dbToGain (float db) noexcept   { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  noexcept   { return 20.0f * std::log10 (juce::jmax (kTinyLevel, g)); }

/** Hermite-style smoothstep between two edges. */
inline float smoothstep (float edge0, float edge1, float x) noexcept
{
    if (edge1 <= edge0)
        return x >= edge1 ? 1.0f : 0.0f;

    const auto t = juce::jlimit (0.0f, 1.0f, (x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

/** Equal-power crossfade pair for a position 0..1. */
inline void equalPower (float t, float& gainA, float& gainB) noexcept
{
    const auto angle = juce::jlimit (0.0f, 1.0f, t) * juce::MathConstants<float>::halfPi;
    gainA = std::cos (angle);
    gainB = std::sin (angle);
}

/** Hann window value for phase 0..1. */
inline float hann (float phase) noexcept
{
    return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * juce::jlimit (0.0f, 1.0f, phase));
}

/** One-pole coefficient for a given time constant in milliseconds. */
inline float onePoleCoeff (double sampleRate, float timeMs) noexcept
{
    if (timeMs <= 0.0f || sampleRate <= 0.0)
        return 1.0f;

    return (float) (1.0 - std::exp (-1.0 / (0.001 * (double) timeMs * sampleRate)));
}

//==============================================================================
/** Deterministic, allocation-free PRNG (xorshift64*).

    Fills are seeded from (user seed, musical position) so that the same
    transport position always produces the same creative decision. */
struct Rng
{
    std::uint64_t state = 0x853c49e6748fea9bULL;

    void seed (std::uint64_t s) noexcept
    {
        // SplitMix64 finaliser so that adjacent seeds decorrelate immediately.
        s += 0x9E3779B97F4A7C15ULL;
        s = (s ^ (s >> 30)) * 0xBF58476D1CE4E5B9ULL;
        s = (s ^ (s >> 27)) * 0x94D049BB133111EBULL;
        s ^= (s >> 31);
        state = (s == 0 ? 0x9E3779B97F4A7C15ULL : s);
    }

    std::uint32_t nextU32() noexcept
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return (std::uint32_t) ((state * 0x2545F4914F6CDD1DULL) >> 32);
    }

    /** Uniform in [0, 1). */
    float nextFloat() noexcept { return (float) nextU32() * 2.3283064e-10f; }

    /** Uniform in [-1, 1). */
    float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }

    /** Uniform in [lo, hi). */
    float range (float lo, float hi) noexcept { return lo + (hi - lo) * nextFloat(); }

    /** Uniform integer in [0, n). */
    int nextInt (int n) noexcept { return n > 0 ? (int) (nextU32() % (std::uint32_t) n) : 0; }

    bool chance (float probability) noexcept { return nextFloat() < probability; }
};

/** Mixes several integers into a single well-distributed 64-bit key. */
inline std::uint64_t hashCombine (std::uint64_t a, std::uint64_t b) noexcept
{
    a ^= b + 0x9E3779B97F4A7C15ULL + (a << 6) + (a >> 2);
    return a;
}

//==============================================================================
/** Asymmetric one-pole envelope follower (peak style). */
struct EnvelopeFollower
{
    void setTimes (double sampleRate, float attackMs, float releaseMs) noexcept
    {
        attackCoeff  = onePoleCoeff (sampleRate, attackMs);
        releaseCoeff = onePoleCoeff (sampleRate, releaseMs);
    }

    void reset (float v = 0.0f) noexcept { y = v; }

    float process (float rectified) noexcept
    {
        const auto c = rectified > y ? attackCoeff : releaseCoeff;
        y += c * (rectified - y);

        if (! std::isfinite (y))
            y = 0.0f;

        return y;
    }

    float value() const noexcept { return y; }

    float attackCoeff = 1.0f, releaseCoeff = 0.01f, y = 0.0f;
};

//==============================================================================
/** Topology-preserving state variable filter (Zavalishin), giving simultaneous
    low-pass, band-pass and high-pass outputs from one structure. */
struct SVF
{
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    void set (double sampleRate, float freqHz, float q) noexcept
    {
        const auto nyquist = (float) (sampleRate * 0.5);
        const auto f = juce::jlimit (10.0f, nyquist * 0.98f, freqHz);
        g = std::tan (juce::MathConstants<float>::pi * f / (float) sampleRate);
        k = 1.0f / juce::jmax (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void process (float x, float& lp, float& bp, float& hp) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        if (! std::isfinite (ic1)) ic1 = 0.0f;
        if (! std::isfinite (ic2)) ic2 = 0.0f;

        bp = v1;
        lp = v2;
        hp = x - k * v1 - v2;
    }

    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

//==============================================================================
/** Simple 2nd order one-pole cascade high-pass used by Low Protect. */
struct HighPass2
{
    void set (double sampleRate, float freqHz) noexcept
    {
        a = std::exp (-juce::MathConstants<float>::twoPi
                        * juce::jlimit (5.0f, (float) (sampleRate * 0.45), freqHz) / (float) sampleRate);
    }

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process (float x) noexcept
    {
        z1 = x + a * (z1 - x);
        const auto s1 = x - z1;
        z2 = s1 + a * (z2 - s1);

        if (! std::isfinite (z1)) z1 = 0.0f;
        if (! std::isfinite (z2)) z2 = 0.0f;

        return s1 - z2;
    }

    float a = 0.0f, z1 = 0.0f, z2 = 0.0f;
};

//==============================================================================
/** Stereo circular history of recent main input.

    Fills are cut from this buffer so that generated material always belongs to
    the track. Reads are position-clamped: it is impossible to read outside the
    valid window, so a stale request degrades gracefully instead of returning
    garbage. */
class SourceHistory
{
public:
    void prepare (int numChannels, int numSamples)
    {
        channels = juce::jmax (1, numChannels);
        size     = juce::jmax (1024, numSamples);
        buffer.setSize (channels, size, false, true, false);
        clear();
    }

    void clear()
    {
        buffer.clear();
        writePos = 0;
        totalWritten = 0;
        readFloor = 0;
    }

    /** C-10 Capture window: absolute positions below `floorPosition` are treated as
        unwritten by read(), copyTo() and isPositionValid() (0 = no limit, the default). */
    void setReadFloor (std::int64_t floorPosition) noexcept   { readFloor = juce::jmax ((std::int64_t) 0, floorPosition); }
    std::int64_t getReadFloor() const noexcept                 { return readFloor; }

    int getSize()      const noexcept { return size; }
    int getChannels()  const noexcept { return channels; }

    /** Absolute index of the next sample that will be written. */
    std::int64_t getWritePosition() const noexcept { return totalWritten; }

    void write (const juce::AudioBuffer<float>& src, int startSample, int numSamples) noexcept
    {
        const auto n = juce::jmin (numSamples, src.getNumSamples() - startSample);

        if (n <= 0)
            return;

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto srcCh = juce::jmin (ch, src.getNumChannels() - 1);
            const auto* in = src.getReadPointer (juce::jmax (0, srcCh), startSample);
            auto* out = buffer.getWritePointer (ch);

            int remaining = n, pos = writePos, offset = 0;

            while (remaining > 0)
            {
                const auto chunk = juce::jmin (remaining, size - pos);
                juce::FloatVectorOperations::copy (out + pos, in + offset, chunk);
                pos = (pos + chunk) % size;
                offset += chunk;
                remaining -= chunk;
            }
        }

        writePos = (writePos + n) % size;
        totalWritten += n;
    }

    /** Reads with linear interpolation at an absolute sample position. */
    float read (int channel, double absolutePosition) const noexcept
    {
        if (channels <= 0 || size <= 0)
            return 0.0f;

        const auto ch = juce::jlimit (0, channels - 1, channel);

        // Valid window: (totalWritten - size, totalWritten - 1]
        const double newest = (double) totalWritten - 1.0;
        const double oldest = (double) totalWritten - (double) size + 2.0;
        const double p = juce::jlimit (juce::jmax (juce::jmax (0.0, oldest), (double) readFloor),
                                       juce::jmax (0.0, newest), absolutePosition);

        const auto i0 = (std::int64_t) std::floor (p);
        const auto frac = (float) (p - (double) i0);

        const auto idx0 = wrapIndex (i0);
        const auto idx1 = wrapIndex (i0 + 1);

        const auto* data = buffer.getReadPointer (ch);
        const auto s0 = data[idx0];
        const auto s1 = (i0 + 1 <= (std::int64_t) newest) ? data[idx1] : s0;

        return s0 + (s1 - s0) * frac;
    }

    /** Block-copies `numSamples` starting at an absolute position into `dest`.

        Used when a voice captures its source material: a bounded memcpy rather
        than tens of thousands of interpolated reads. Positions outside the valid
        window are filled with silence. */
    void copyTo (juce::AudioBuffer<float>& dest, std::int64_t startPosition, int numSamples) const noexcept
    {
        const auto destChannels = dest.getNumChannels();

        for (int ch = 0; ch < destChannels; ++ch)
            dest.clear (ch, 0, juce::jmin (numSamples, dest.getNumSamples()));

        if (size <= 0 || numSamples <= 0)
            return;

        const auto oldest = totalWritten - (std::int64_t) size + 1;
        const auto from = juce::jmax (startPosition, juce::jmax (juce::jmax ((std::int64_t) 0, oldest), readFloor));
        const auto to   = juce::jmin (startPosition + numSamples, totalWritten);

        if (to <= from)
            return;

        const auto count = (int) juce::jmin ((std::int64_t) numSamples, to - from);
        const auto destOffset = (int) (from - startPosition);

        for (int ch = 0; ch < destChannels; ++ch)
        {
            const auto srcCh = juce::jlimit (0, channels - 1, ch);
            const auto* src = buffer.getReadPointer (srcCh);
            auto* out = dest.getWritePointer (ch);

            int remaining = count, pos = wrapIndex (from), offset = destOffset;

            while (remaining > 0 && offset < dest.getNumSamples())
            {
                const auto chunk = juce::jmin (remaining, size - pos, dest.getNumSamples() - offset);
                juce::FloatVectorOperations::copy (out + offset, src + pos, chunk);
                pos = (pos + chunk) % size;
                offset += chunk;
                remaining -= chunk;
            }
        }
    }

    bool isPositionValid (std::int64_t absolutePosition) const noexcept
    {
        return absolutePosition >= 0
            && absolutePosition >= readFloor
            && absolutePosition < totalWritten
            && absolutePosition > totalWritten - (std::int64_t) size;
    }

private:
    int wrapIndex (std::int64_t absolute) const noexcept
    {
        auto m = (int) (absolute % (std::int64_t) size);
        return m < 0 ? m + size : m;
    }

    juce::AudioBuffer<float> buffer;
    int channels = 0, size = 0, writePos = 0;
    std::int64_t totalWritten = 0;
    std::int64_t readFloor = 0;
};

//==============================================================================
/** Fixed-capacity FIFO of future event positions, used for lookahead.

    The analysis engine pushes event positions as it sees them; the scheduler,
    which runs `lookahead` samples behind, pops the ones it has passed and can
    then ask "when does the next important event happen?" in O(1). */
template <int Capacity>
class EventQueue
{
public:
    void clear() noexcept { head = tail = 0; }

    void push (std::int64_t position) noexcept
    {
        const auto next = (tail + 1) % Capacity;

        if (next == head)                 // full - drop the oldest entry
            head = (head + 1) % Capacity;

        data[tail] = position;
        tail = next;
    }

    /** Discards entries strictly before `position`. */
    void discardBefore (std::int64_t position) noexcept
    {
        while (head != tail && data[head] < position)
            head = (head + 1) % Capacity;
    }

    /** First queued position >= `from`, or -1 when nothing is known. */
    std::int64_t nextAtOrAfter (std::int64_t from) const noexcept
    {
        auto i = head;

        while (i != tail)
        {
            if (data[i] >= from)
                return data[i];

            i = (i + 1) % Capacity;
        }

        return -1;
    }

    bool isEmpty() const noexcept { return head == tail; }

private:
    std::int64_t data[(std::size_t) Capacity] {};
    int head = 0, tail = 0;
};

} // namespace gapfill
