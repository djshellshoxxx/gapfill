#include "MacroEngine.h"

#include "../../dsp/DspUtils.h"
#include "../Salts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>

namespace gapfill::macros
{

namespace
{
    const TargetInfo kTargets[kNumTargets] =
    {
        { "fill", 0.0f, 100.0f, "Fill" }, { "intensity", 0.0f, 100.0f, "Intensity" },
        { "complexity", 0.0f, 100.0f, "Complexity" }, { "chaos", 0.0f, 100.0f, "Chaos" },
        { "density", 0.0f, 100.0f, "Density" }, { "width", 0.0f, 200.0f, "Wet Width" },
        { "wReverse", 0.0f, 100.0f, "Weight Reverse" }, { "wStutter", 0.0f, 100.0f, "Weight Stutter" },
        { "wDelay", 0.0f, 100.0f, "Weight Delay" }, { "wGranular", 0.0f, 100.0f, "Weight Granular" },
        { "wFreeze", 0.0f, 100.0f, "Weight Freeze" }, { "wPitchRise", 0.0f, 100.0f, "Weight Pitch Rise" },
        { "wPitchFall", 0.0f, 100.0f, "Weight Pitch Fall" }, { "wFilter", 0.0f, 100.0f, "Weight Filter" },
        { "wTexture", 0.0f, 100.0f, "Weight Texture" }, { "wCrush", 0.0f, 100.0f, "Weight Crush" },
        { "wDistort", 0.0f, 100.0f, "Weight Distort" }, { "wScramble", 0.0f, 100.0f, "Weight Scramble" },
        { "wSpectral", 0.0f, 100.0f, "Weight Spectral" }, { "wComb", 0.0f, 100.0f, "Weight Comb" },
        { "wTapeStop", 0.0f, 100.0f, "Weight Tape Stop" },
        // F-M23 (indices 21..35, append-only)
        { "swellAmount", 0.0f, 100.0f, "Reverse Swell" }, { "cloudAmount", 0.0f, 100.0f, "Granular Cloud" },
        { "ratAmount", 0.0f, 100.0f, "Ratchet" }, { "rcAmount", 0.0f, 100.0f, "Rhythmic Crush" },
        { "tdAmount", 0.0f, 100.0f, "Tape Destruction" }, { "resAmount", 0.0f, 100.0f, "Resonator" },
        { "fsdAmount", 0.0f, 100.0f, "Frequency Shift Delay" }, { "dubAmount", 0.0f, 100.0f, "Dub Feedback" },
        { "fmtAmount", 0.0f, 100.0f, "Formant" }, { "sfzAmount", 0.0f, 100.0f, "Spectral Freeze" },
        { "cvAmount", 0.0f, 100.0f, "Convolution" }, { "tensionAmount", 0.0f, 100.0f, "Tension" },
        { "energyAdapt", 0.0f, 100.0f, "Energy Adapt" }, { "scaleAmount", 0.0f, 100.0f, "Scale Amount" },
        { "grooveAmount", 0.0f, 100.0f, "Groove" },
    };

    std::array<targets::Ref, kNumTargets> targetRefs {};
    std::once_flag initFlag;

    inline float clamp01 (float x) noexcept { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
    inline float sane (float x) noexcept    { return std::isfinite (x) ? x : 0.0f; }

    float hash01 (int seed, long long cycle) noexcept
    {
        auto h = hashCombine (hashCombine ((std::uint64_t) (std::uint32_t) seed, kSalt_Macros), (std::uint64_t) cycle);
        h ^= h >> 30;                                   // splitmix64 finaliser: neighbouring cycles must not share top bits
        h *= 0xBF58476D1CE4E5B9ULL;
        h ^= h >> 27;
        h *= 0x94D049BB133111EBULL;
        h ^= h >> 31;
        return (float) ((h >> 11) & 0xFFFFFFu) / 16777215.0f;
    }
}

const TargetInfo& targetInfo (int index) noexcept
{
    return kTargets[std::clamp (index, 0, kNumTargets - 1)];
}

void initTargets()
{
    std::call_once (initFlag, []
    {
        for (int i = 0; i < kNumTargets; ++i)
            targetRefs[(size_t) i] = targets::refForId (kTargets[i].id);
    });
}

//==============================================================================
MacroConfig defaultConfig()
{
    MacroConfig c;
    const char* names[kNumMacros] = { "Activity", "Chaos", "Space", "Destruction" };

    for (int m = 0; m < kNumMacros; ++m)
        std::strncpy (c.names[m], names[m], kNameBytes);

    auto add = [&] (int macro, int target, float lo, float hi)
    {
        Mapping mp;
        mp.target = (std::uint8_t) target;
        mp.min01 = lo;
        mp.max01 = hi;
        c.map[macro][c.count[macro]++] = mp;
    };

    constexpr int wScramble = 6 + (int) Algo::Scramble, wDelay = 6 + (int) Algo::Delay, wSpectral = 6 + (int) Algo::Spectral;
    constexpr int wCrush = 6 + (int) Algo::Crush, wDistort = 6 + (int) Algo::Distort;

    add (0, 4, 0.25f, 0.90f);  add (0, 0, 0.20f, 0.75f);                                     // Activity: Density, Fill
    add (1, 3, 0.00f, 0.75f);  add (1, 2, 0.20f, 0.85f);  add (1, wScramble, 0.10f, 0.70f);  // Chaos: Chaos, Complexity, wScramble
    add (2, 5, 0.30f, 0.75f);  add (2, wDelay, 0.20f, 0.75f);  add (2, wSpectral, 0.10f, 0.55f);   // Space: Width 60-150 %, wDelay, wSpectral
    add (3, 1, 0.20f, 0.90f);  add (3, wCrush, 0.00f, 0.65f);  add (3, wDistort, 0.00f, 0.65f);    // Destruction

    return c;
}

float applyCurve (int curve, float x) noexcept
{
    x = clamp01 (sane (x));

    switch (curve)
    {
        case 1:  return x * x;
        case 2:  return 1.0f - (1.0f - x) * (1.0f - x);
        case 3:  return x * x * (3.0f - 2.0f * x);
        default: return x;
    }
}

float mappingValue (const Mapping& m, float v01) noexcept
{
    const auto v = clamp01 (sane (v01));
    const auto lo = clamp01 (sane (m.min01)), hi = clamp01 (sane (m.max01));

    if (m.bipolar == 0)
        return clamp01 (lo + (hi - lo) * applyCurve (m.curve, v));

    const auto x = v * 2.0f - 1.0f;
    const auto shaped = (x < 0.0f ? -1.0f : 1.0f) * applyCurve (m.curve, std::abs (x));
    return clamp01 (0.5f * (lo + hi) + shaped * 0.5f * (hi - lo));
}

//==============================================================================
float lfoValue (const LfoSettings& l, double cycles, int seed) noexcept
{
    const auto shifted = cycles + (double) l.phaseDeg / 360.0;
    const auto whole = std::floor (shifted);
    const auto phase = (float) (shifted - whole);

    float wave = 0.0f;

    switch (l.shape)
    {
        case 0:  wave = std::sin (6.28318530718f * phase); break;
        case 1:  wave = 4.0f * std::abs (phase - 0.5f) - 1.0f; break;
        case 2:  wave = 2.0f * phase - 1.0f; break;
        case 3:  wave = phase < 0.5f ? 1.0f : -1.0f; break;
        default: wave = hash01 (seed, (long long) whole) * 2.0f - 1.0f; break;
    }

    return clamp01 (0.5f + 0.5f * clamp01 (sane (l.depth01)) * wave);
}

double lfoCyclesAtPpq (const LfoSettings& l, double ppq) noexcept
{
    return ppq / std::max (1.0e-3, divisionInBeats (l.division));
}

double lfoCyclesAtTime (const LfoSettings& l, double seconds) noexcept
{
    return seconds * (double) std::clamp (sane (l.rateHz), 0.01f, 20.0f);
}

//==============================================================================
bool MacroRuntime::anyLfo() const noexcept
{
    for (int m = 0; m < kNumMacros; ++m)
        if (cfg.lfo[m].enabled != 0 && cfg.count[m] > 0)
            return true;

    return false;
}

void MacroRuntime::applyTarget (EngineSettings& s, int t, float eff01) const noexcept
{
    const auto& info = kTargets[t];
    const auto real = info.lo + clamp01 (eff01) * (info.hi - info.lo);

    if (t == 5)
    {
        s.perf.macroWidthOn = true;
        s.perf.macroWidth01 = real * 0.01f;
        return;
    }

    if (const auto ref = targetRefs[(size_t) t]; ref)
        targets::apply (s, ref, real);
}

void MacroRuntime::evaluateBlock (EngineSettings& s, const float knobIn[kNumMacros], std::uint32_t& ownedMask) noexcept
{
    ownedMask = 0;
    widthOn = false;

    for (int t = 0; t < kNumTargets; ++t)
        active[t] = false;

    for (int m = 0; m < kNumMacros; ++m)
    {
        knob01[m] = clamp01 (sane (knobIn[m]));
        const auto count = std::min<int> (cfg.count[m], kMaxMappings);

        if (count <= 0)
            continue;

        float v = knob01[m];

        if (const auto& l = cfg.lfo[m]; l.enabled != 0)
            v = lfoValue (l, lfoCyclesAtTime (l, freeTime), seed);       // the block stage has no PPQ yet

        for (int j = 0; j < count; ++j)
        {
            const auto& mp = cfg.map[m][j];
            const int t = std::min<int> (mp.target, kNumTargets - 1);

            if (active[t])
                continue;                                               // lower-index macro wins a shared target

            active[t] = true;
            auto eff = mappingValue (mp, v);

            if (mp.smoothMs > 0.0f)
            {
                if (! hasSmoothed[t])
                {
                    smoothed[t] = eff;
                    hasSmoothed[t] = true;
                }

                const auto alpha = 1.0f - std::exp (-chunkSeconds / (std::clamp (mp.smoothMs, 1.0f, 500.0f) * 0.001f));
                smoothed[t] += (eff - smoothed[t]) * alpha;
                eff = clamp01 (smoothed[t]);
            }
            else
            {
                hasSmoothed[t] = false;
            }

            effective01[t] = eff;
            applyTarget (s, t, eff);
            ownedMask |= targets::macroBit (targetRefs[(size_t) t]);

            if (t == 5)
            {
                widthOn = true;
                width01 = s.perf.macroWidth01;
            }
        }
    }

    for (int t = 0; t < kNumTargets; ++t)
        if (! active[t])
            hasSmoothed[t] = false;

    freeTime += (double) chunkSeconds;
}

void MacroRuntime::evaluateDecision (EngineSettings& s, double ppq) const noexcept
{
    bool claimed[kNumTargets] {};

    for (int m = 0; m < kNumMacros; ++m)
    {
        const auto count = std::min<int> (cfg.count[m], kMaxMappings);
        const auto& l = cfg.lfo[m];

        for (int j = 0; j < count; ++j)
        {
            const auto& mp = cfg.map[m][j];
            const int t = std::min<int> (mp.target, kNumTargets - 1);

            if (claimed[t])
                continue;

            claimed[t] = true;

            if (l.enabled == 0 || l.tempoSync == 0)
                continue;                                                // knob / free-run values were applied by the block stage

            applyTarget (s, t, mappingValue (mp, lfoValue (l, lfoCyclesAtPpq (l, ppq), seed)));
        }
    }
}

} // namespace gapfill::macros
