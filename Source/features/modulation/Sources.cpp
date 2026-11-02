#include "Sources.h"
#include "../Salts.h"

#include <algorithm>
#include <cmath>

namespace gapfill::modulation
{
namespace
{
    float finite (float value) noexcept { return std::isfinite (value) ? value : 0.0f; }
    float unit (float value) noexcept { return std::clamp (finite (value), 0.0f, 1.0f); }

    float randomValue (std::uint64_t seed, int lfoId, std::int64_t cycle) noexcept
    {
        // Random access, independent of every legacy RNG and of evaluation order.
        const auto salt = lfoId == 1 ? kSalt_LfoB : kSalt_LfoA;
        auto hash = seed ^ salt;
        hash ^= (std::uint64_t) cycle + 0x9E3779B97F4A7C15ULL + (hash << 6u) + (hash >> 2u);
        hash ^= hash >> 30u; hash *= 0xBF58476D1CE4E5B9ULL;
        hash ^= hash >> 27u; hash *= 0x94D049BB133111EBULL;
        hash ^= hash >> 31u;
        return 2.0f * ((float) (hash >> 40u) / 16777216.0f) - 1.0f;
    }
}

double divisionBeats (int division, int tsNum, int tsDen) noexcept
{
    const double bar = tsNum > 0 && tsDen > 0 ? (double) tsNum * 4.0 / tsDen : 4.0;
    constexpr double beats[16] { .25, 1.0 / 3, .5, .75, 2.0 / 3, 1, 1.5, 2,
                                 4, 8, 16, 32, .125, 1.0 / 6, .375, 4.0 / 3 };
    const auto index = std::clamp (division, 0, 15);
    return index >= 8 && index <= 11 ? bar * (double) (1 << (index - 8)) : beats[index];
}

float lfoValue (const LfoSettings& lfo, const DecisionInputs& at, int lfoId) noexcept
{
    const auto rate = std::clamp (std::isfinite (lfo.freeHz) ? lfo.freeHz : .5f, .05f, 10.0f);
    const auto sr = std::isfinite (at.sampleRate) && at.sampleRate > 0 ? at.sampleRate : 48000.0;
    const auto cycles = at.transportValid && std::isfinite (at.ppq)
        ? at.ppq / divisionBeats (lfo.division, at.tsNum, at.tsDen)
        : ((double) at.outputPos / sr) * rate;
    // Keep hostile centuries-long positions within exact integer conversion bounds.
    const auto bounded = std::isfinite (cycles) ? std::remainder (cycles, 9007199254740992.0) : 0.0;
    const auto whole = std::floor (bounded);
    const auto phase = (float) (bounded - whole);
    const auto cycle = (std::int64_t) whole;
    float wave = 0.0f;
    switch (lfo.shape)
    {
        case LfoShape::Sine: wave = std::sin (6.2831853071795864769f * phase); break;
        case LfoShape::Triangle: wave = 1.0f - 4.0f * std::abs (phase - .5f); break;
        case LfoShape::Saw: wave = 2 * phase - 1; break;
        case LfoShape::Square: wave = phase < .5f ? 1.0f : -1.0f; break;
        case LfoShape::RandomStep: wave = randomValue (at.seed, lfoId, cycle); break;
        case LfoShape::RandomGlide:
        {
            const auto start = randomValue (at.seed, lfoId, cycle);
            const auto end = randomValue (at.seed, lfoId, cycle + 1);
            const auto smooth = phase * phase * (3 - 2 * phase);
            wave = start + (end - start) * smooth;
            break;
        }
        default: break;
    }
    return std::clamp (wave, -1.0f, 1.0f);
}

float macroValue (MacroSource source, float knob, float depth, float lfoA, float lfoB, float energy) noexcept
{
    const auto centre = unit (knob);
    if (source == MacroSource::Knob) return centre;
    float value = 0;
    switch (source)
    {
        case MacroSource::LfoA: value = std::clamp (finite (lfoA), -1.0f, 1.0f); break;
        case MacroSource::LfoB: value = std::clamp (finite (lfoB), -1.0f, 1.0f); break;
        case MacroSource::Energy: value = 2 * unit (energy) - 1; break;
        default: return centre;
    }
    return unit (centre + std::clamp (finite (depth), -1.0f, 1.0f) * value);
}
} // namespace gapfill::modulation
