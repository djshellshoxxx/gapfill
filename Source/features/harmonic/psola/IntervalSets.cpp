#include "IntervalSets.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace gapfill::harmonic::psola
{
namespace
{
    bool knownKey (const Scale& key) noexcept
    {
        return key.rootPc >= 0 && key.rootPc < 12 && key.pitchClasses != 0 && (key.pitchClasses & 0xf000u) == 0;
    }
    bool allowed (int note, const Scale& key) noexcept
    {
        const int pc = (note % 12 + 12) % 12;
        return (key.pitchClasses & (std::uint16_t (1) << pc)) != 0;
    }
}
RatioSet intervalRatios (const Settings& settings, float f0, const Scale& key) noexcept
{
    RatioSet result;
    if (! settings.enabled || ! std::isfinite (f0) || f0 < 50 || f0 > 1000) return result;
    const bool haveKey = knownKey (key);
    auto selection = settings.intervals;
    if (selection == IntervalSet::Scale && ! haveKey) selection = IntervalSet::OpenFifths;
    const double sourceMidi = 69 + 12 * std::log2 ((double) f0 / 440);
    switch (selection)
    {
        case IntervalSet::Octaves: result.values = { 1, 2, 0.5, 0 }; result.count = 3; break;
        case IntervalSet::Major: result.values = { 1, 1.25, 1.5, 2 }; result.count = 4; break;
        case IntervalSet::Minor: result.values = { 1, 1.2, 1.5, 2 }; result.count = 4; break;
        case IntervalSet::Suspended: result.values = { 1, 4.0 / 3, 1.5, 2 }; result.count = 4; break;
        case IntervalSet::Scale:
        {
            const int sourceNote = (int) std::lround (sourceMidi);
            for (int semitones = 0; semitones <= 12 && result.count < kMaxVoices; ++semitones)
                if (allowed (sourceNote + semitones, key))
                    result.values[(std::size_t) result.count++] = std::exp2 (semitones / 12.0);
            break;
        }
        case IntervalSet::OpenFifths:
        default: result.values = { 1, 1.5, 2, 3 }; result.count = 4; break;
    }
    result.count = std::min (result.count, std::clamp (settings.voices, 1, kMaxVoices));
    const double snap = haveKey && std::isfinite (settings.snap01) ? std::clamp ((double) settings.snap01, 0.0, 1.0) : 0;
    if (snap == 0) return result;
    for (int i = 0; i < result.count; ++i)
    {
        const double midi = sourceMidi + 12 * std::log2 (result.values[(std::size_t) i]);
        const int centre = (int) std::floor (midi);
        int nearest = centre;
        double distance = std::numeric_limits<double>::infinity();
        for (int note = centre - 12; note <= centre + 12; ++note)
            if (allowed (note, key))
            {
                const double candidate = std::abs (note - midi);
                if (candidate < distance || (candidate == distance && note > nearest))
                {
                    nearest = note; distance = candidate;
                }
            }
        const double shifted = midi + snap * (nearest - midi);
        result.values[(std::size_t) i] = std::clamp (std::exp2 ((shifted - sourceMidi) / 12), 0.25, 4.0);
    }
    return result;
}
} // namespace gapfill::harmonic::psola
