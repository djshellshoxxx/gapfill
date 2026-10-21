#include "ScaleLock.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace gapfill::scale
{

namespace
{
    // Index 0 is Off (no degrees); 1..12 follow docs/dev/12 N-MUS.
    constexpr ScaleTable kTables[kNumScaleTypes] =
    {
        { 0, { 0, 0, 0, 0, 0, 0, 0 } },
        { 7, { 0, 2, 4, 5, 7, 9, 11 } },     // Major
        { 7, { 0, 2, 3, 5, 7, 8, 10 } },     // Natural Minor
        { 7, { 0, 2, 3, 5, 7, 9, 10 } },     // Dorian
        { 7, { 0, 1, 3, 5, 7, 8, 10 } },     // Phrygian
        { 7, { 0, 2, 4, 6, 7, 9, 11 } },     // Lydian
        { 7, { 0, 2, 4, 5, 7, 9, 10 } },     // Mixolydian
        { 7, { 0, 2, 3, 5, 7, 8, 11 } },     // Harmonic Minor
        { 7, { 0, 2, 3, 5, 7, 9, 11 } },     // Melodic Minor
        { 5, { 0, 2, 4, 7, 9, 0, 0 } },      // Major Pentatonic
        { 5, { 0, 3, 5, 7, 10, 0, 0 } },     // Minor Pentatonic
        { 6, { 0, 3, 5, 6, 7, 10, 0 } },     // Blues
        { 6, { 0, 2, 4, 6, 8, 10, 0 } },     // Whole Tone
    };

    constexpr const char* kScaleNames[kNumScaleTypes] =
    {
        "Off", "Major", "Natural Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
        "Harmonic Minor", "Melodic Minor", "Major Pentatonic", "Minor Pentatonic", "Blues", "Whole Tone"
    };

    constexpr const char* kRootNames[kNumRoots] =
        { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    int clampi (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    int mod12 (int v) noexcept { return ((v % 12) + 12) % 12; }

    /** Allowed relative interval for source pitch class `pc`: lands in the scale, or is a multiple of 12. */
    bool allowed (int interval, int pc, const ScaleSet& set) noexcept
    {
        if (mod12 (interval) == 0)
            return true;

        return set.containsPitchClass (pc + interval);
    }

    int resolveSourcePc (const ScaleSet& set, const PitchContext& ctx) noexcept
    {
        return ctx.sourcePc >= 0 ? mod12 (ctx.sourcePc) : (int) set.root;
    }
}

const ScaleTable& scaleTable (int type) noexcept
{
    return kTables[(type >= 1 && type < kNumScaleTypes) ? type : 0];
}

const char* scaleName (int type) noexcept
{
    return kScaleNames[clampi (type, 0, kNumScaleTypes - 1)];
}

const char* rootName (int root) noexcept
{
    return kRootNames[clampi (root, 0, kNumRoots - 1)];
}

ScaleSet makeScaleSet (int root, int type) noexcept
{
    ScaleSet s;
    s.root = (std::uint8_t) clampi (root, 0, kNumRoots - 1);
    s.type = (std::uint8_t) clampi (type, 0, kNumScaleTypes - 1);

    const auto& t = scaleTable (s.type);

    for (int i = 0; i < t.count; ++i)
        s.mask = (std::uint16_t) (s.mask | (1u << t.degrees[i]));

    return s;
}

int snapInterval (int semis, const ScaleSet& set, const PitchContext& ctx) noexcept
{
    if (! set.active())
        return semis;

    const auto pc = resolveSourcePc (set, ctx);

    if (allowed (semis, pc, set))
        return semis;

    // Nearest allowed interval. Equal distance: smaller |interval| first, then upward.
    // 0 and multiples of 12 are always allowed, so the search ends within 6 steps.
    for (int d = 1; d <= 6; ++d)
    {
        const int lo = semis - d, hi = semis + d;
        const bool loOk = allowed (lo, pc, set), hiOk = allowed (hi, pc, set);

        if (loOk && hiOk)
        {
            const int alo = lo < 0 ? -lo : lo, ahi = hi < 0 ? -hi : hi;
            return alo < ahi ? lo : hi;                 // tie on |i|: upward
        }

        if (loOk)  return lo;
        if (hiOk)  return hi;
    }

    return semis;
}

float snapCents (float cents, const ScaleSet& set, const PitchContext& ctx) noexcept
{
    if (! set.active() || ! std::isfinite (cents))
        return cents;

    const auto semis = (int) std::lround (cents / 100.0f);
    return 100.0f * (float) snapInterval (semis, set, ctx);
}

float snapFrequency (float hz, const ScaleSet& set) noexcept
{
    if (! set.active() || ! std::isfinite (hz) || hz <= 0.0f)
        return hz;

    const double note = 69.0 + 12.0 * std::log2 ((double) hz / 440.0);       // continuous MIDI note
    const int base = (int) std::floor (note);

    // Candidate notes within a tritone either side of the input; pick the nearest allowed one.
    int best = base;
    double bestDist = 1.0e9;

    for (int n = base - 6; n <= base + 7; ++n)
    {
        if (! set.containsPitchClass (mod12 (n)))
            continue;

        const double dist = std::fabs ((double) n - note);

        // nearer wins; at equal distance the higher note wins (iteration is ascending)
        if (dist <= bestDist + 1.0e-12)
        {
            best = n;
            bestDist = dist;
        }
    }

    return (float) (440.0 * std::pow (2.0, ((double) best - 69.0) / 12.0));
}

float snapRatio (float ratio, const ScaleSet& set, const PitchContext& ctx, float minRatio, float maxRatio) noexcept
{
    if (! set.active() || ! std::isfinite (ratio) || ratio <= 0.0f)
        return ratio;

    const auto pc = resolveSourcePc (set, ctx);
    const int loK = (int) std::ceil (12.0 * std::log2 ((double) minRatio) - 1.0e-9);
    const int hiK = (int) std::floor (12.0 * std::log2 ((double) maxRatio) + 1.0e-9);
    const double want = 12.0 * std::log2 ((double) ratio);

    int best = 0;
    double bestDist = 1.0e9;

    for (int k = loK; k <= hiK; ++k)
    {
        if (! allowed (k, pc, set))
            continue;

        const double dist = std::fabs ((double) k - want);
        const bool better = dist < bestDist - 1.0e-9
                         || (std::fabs (dist - bestDist) <= 1.0e-9 && std::abs (k) < std::abs (best));

        if (better)
        {
            best = k;
            bestDist = dist;
        }
    }

    return (float) std::pow (2.0, (double) best / 12.0);
}

const char* describeShort (const ScaleSet& set, char* buffer, int bufferSize) noexcept
{
    if (buffer == nullptr || bufferSize <= 0)
        return "";

    if (! set.active())
        buffer[0] = 0;
    else
        std::snprintf (buffer, (size_t) bufferSize, "%s %s", rootName (set.root), scaleName (set.type));

    return buffer;
}

const char* describePitchClasses (const ScaleSet& set, char* buffer, int bufferSize) noexcept
{
    if (buffer == nullptr || bufferSize <= 0)
        return "";

    buffer[0] = 0;

    if (! set.active())
        return buffer;

    int used = 0;

    for (int k = 0; k < 12; ++k)
    {
        if (! set.hasDegree (k))
            continue;

        const char* name = rootName (mod12 ((int) set.root + k));
        const int n = std::snprintf (buffer + used, (size_t) (bufferSize - used), used > 0 ? " %s" : "%s", name);

        if (n < 0 || n >= bufferSize - used)
            break;

        used += n;
    }

    return buffer;
}

} // namespace gapfill::scale
