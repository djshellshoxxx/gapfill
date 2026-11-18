#pragma once

// F-G01 Glitch rack: the shared rhythmic grid (C: swing and timing).
//
// Every grid decision is a pure function of the musical position, so output does
// not depend on the host block size, and a transport jump lands on the same
// step a straight render would reach (no accumulated drift). Random decisions
// hash (seed, salt, step index) instead of drawing from a running generator for
// the same reason.

#include <cmath>
#include <cstdint>

namespace gapfill::glitch
{

/** splitmix64: a well-known public-domain integer mixer (Steele, Lea, Flood 2014). */
inline std::uint64_t mix64 (std::uint64_t x) noexcept
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/** Deterministic uniform value in [0, 1) for (seed, salt, index). */
inline double hash01 (int seed, std::uint32_t salt, std::int64_t index) noexcept
{
    const auto h = mix64 (mix64 ((std::uint64_t) (std::uint32_t) seed * 0x100000001B3ull ^ salt) ^ (std::uint64_t) index);
    return (double) (h >> 11) * (1.0 / 9007199254740992.0);
}

struct GridPoint
{
    std::int64_t step = 0;     ///< index of the step containing the position
    double phase = 0.0;        ///< 0..1 inside the step
    double length = 1.0;       ///< step length in straight-step units (swing makes it 0.5 .. 1.5)
};

/** Maps a straight position (in steps) onto the swung, varied grid.
    swing01 0..1 moves every second step start from 1/2 to 3/4 of its pair.
    timeVar01 0..1 displaces every step start by up to +-10 % of a step
    (deterministic per step). Boundaries stay strictly increasing: the shortest
    swung step is 0.5 and the two displacements subtract at most 0.2. */
class Grid
{
public:
    Grid() = default;
    Grid (double swing01, double timeVar01, int seedValue, std::uint32_t saltValue) noexcept
        : ratio (0.5 + 0.25 * clamp01 (swing01)), var (0.1 * clamp01 (timeVar01)), seed (seedValue), salt (saltValue) {}

    double startOf (std::int64_t k) const noexcept
    {
        const auto pair = floorDiv2 (k);
        const auto base = (double) (2 * pair) + ((k - 2 * pair) != 0 ? 2.0 * ratio : 0.0);

        if (var <= 0.0)
            return base;

        return base + (hash01 (seed, salt, k) - 0.5) * 2.0 * var;
    }

    GridPoint locate (double p) const noexcept
    {
        const auto pair = std::floor (p * 0.5);
        const auto q = p * 0.5 - pair;
        auto k = (std::int64_t) pair * 2 + (q >= ratio ? 1 : 0);

        if (var > 0.0)
        {
            if (p < startOf (k))
                --k;
            else if (p >= startOf (k + 1))
                ++k;
        }

        const auto a = startOf (k);
        const auto b = startOf (k + 1);
        GridPoint g;
        g.step = k;
        g.length = b - a;
        g.phase = g.length > 0.0 ? clamp01 ((p - a) / g.length) : 0.0;
        return g;
    }

private:
    static double clamp01 (double x) noexcept { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }
    static std::int64_t floorDiv2 (std::int64_t k) noexcept { return k >= 0 ? k / 2 : -((-k + 1) / 2); }

    double ratio = 0.5;
    double var = 0.0;
    int seed = 1;
    std::uint32_t salt = 0;
};

/** Position source for one sample: the host beat while the transport runs,
    otherwise an internal beat counter at the resolved tempo, plus a free-running
    sample counter for the millisecond/Hz modes. relocated is set when the beat
    position did not follow on from the previous sample (seek, loop, start/stop). */
struct ClockSample
{
    double beat = 0.0;
    std::int64_t freeSamples = 0;
    bool relocated = false;
};

class BeatClock
{
public:
    void reset() noexcept
    {
        internalBeat = 0.0;
        freeSamples = 0;
        lastBeat = 0.0;
        primed = false;
    }

    /** Positions every sample of a chunk; call once per chunk with the chunk's timing. */
    void begin (bool playing, double ppqAtStart, double samplesPerBeat) noexcept
    {
        hostPlaying = playing;
        hostPpq = ppqAtStart;
        beatPerSample = samplesPerBeat > 1.0 ? 1.0 / samplesPerBeat : 1.0 / 24000.0;
        index = 0;
    }

    ClockSample next() noexcept
    {
        ClockSample c;
        c.beat = hostPlaying ? hostPpq + (double) index * beatPerSample : internalBeat;
        c.freeSamples = freeSamples;

        // A jump of more than a few samples' worth of beats is a relocation.
        c.relocated = ! primed || std::abs (c.beat - (lastBeat + beatPerSample)) > beatPerSample * 4.0 + 1.0e-9;
        primed = true;
        lastBeat = c.beat;
        internalBeat = c.beat + beatPerSample;
        ++freeSamples;
        ++index;
        return c;
    }

private:
    bool hostPlaying = false;
    double hostPpq = 0.0, beatPerSample = 1.0 / 24000.0;
    double internalBeat = 0.0, lastBeat = 0.0;
    std::int64_t freeSamples = 0;
    std::int64_t index = 0;
    bool primed = false;
};

} // namespace gapfill::glitch
