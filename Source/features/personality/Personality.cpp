#include "Personality.h"

#include <algorithm>

namespace gapfill::personality
{

namespace
{
    // Final numbers from docs/dev/04 C-1 (transposed: one row per personality, algorithms in Algo
    // order Reverse, Stutter, Delay, Granular, Freeze, PitchRise, PitchFall, Filter, Texture, Crush,
    // Distort, Scramble, Spectral, Comb, TapeStop; then density, complexity, chaos, intensity).
    constexpr Overlay kOverlay[numNamed] =
    {
        /* Subtle      */ { { 0.6f, 0.2f, 1.0f, 1.2f, 0.6f, 0.6f, 0.6f, 1.2f, 1.3f, 0.2f, 0.2f, 0.2f, 0.6f, 0.6f, 0.6f }, 0.80f, 0.70f, 0.60f, 0.70f },
        /* Percussive  */ { { 1.2f, 1.6f, 0.8f, 0.6f, 0.3f, 0.8f, 0.8f, 0.8f, 0.3f, 0.8f, 0.6f, 1.4f, 0.2f, 0.4f, 0.8f }, 1.10f, 1.00f, 0.90f, 1.10f },
        /* Glitchy     */ { { 0.9f, 1.5f, 0.7f, 1.3f, 0.6f, 0.6f, 0.6f, 0.8f, 0.3f, 1.5f, 1.0f, 1.6f, 0.6f, 0.6f, 0.8f }, 1.15f, 1.20f, 1.20f, 1.00f },
        /* Psychedelic */ { { 1.0f, 0.4f, 1.2f, 1.6f, 1.5f, 1.2f, 1.2f, 1.2f, 1.4f, 0.3f, 0.3f, 0.5f, 1.8f, 1.2f, 0.8f }, 0.90f, 1.20f, 1.10f, 0.90f },
        /* Dub         */ { { 1.2f, 0.5f, 2.0f, 0.6f, 0.8f, 0.8f, 0.8f, 1.6f, 0.8f, 0.2f, 0.4f, 0.3f, 0.4f, 1.2f, 1.2f }, 0.85f, 0.80f, 0.60f, 0.90f },
        /* Aggressive  */ { { 0.8f, 1.4f, 0.6f, 0.7f, 0.3f, 0.9f, 1.2f, 0.8f, 0.2f, 1.6f, 1.8f, 1.2f, 0.3f, 0.6f, 0.8f }, 1.20f, 1.10f, 1.10f, 1.30f },
        /* Ambient     */ { { 1.1f, 0.1f, 1.2f, 1.4f, 1.8f, 0.8f, 0.8f, 1.2f, 2.0f, 0.1f, 0.1f, 0.2f, 1.6f, 0.8f, 0.6f }, 0.60f, 0.70f, 0.50f, 0.50f },
    };

    constexpr const char* kNames[NumPersonalities] =
        { "Off", "Subtle", "Percussive", "Glitchy", "Psychedelic", "Dub", "Aggressive", "Ambient", "Unhinged" };

    constexpr float kUnhingedFlatten = 0.7f;

    inline float clamp01 (float v) noexcept { return std::clamp (v, 0.0f, 1.0f); }
}

const Overlay* overlayFor (int p) noexcept
{
    return (p >= Subtle && p <= Ambient) ? &kOverlay[p - Subtle] : nullptr;
}

const char* name (int p) noexcept
{
    return (p >= 0 && p < NumPersonalities) ? kNames[p] : "";
}

void apply (EngineSettings& s, int p) noexcept
{
    if (p <= Off || p >= NumPersonalities)
        return;

    float density = 1.0f, complexity = 1.0f, chaos = 1.0f, intensity = 1.0f;
    float wMul[numAlgos];
    std::fill (std::begin (wMul), std::end (wMul), 1.0f);

    const bool unhinged = (p == Unhinged);
    float mean = 0.0f;

    if (unhinged)
    {
        density = 1.2f; complexity = 1.3f; chaos = 1.5f; intensity = 1.1f;

        int n = 0;

        for (int i = 0; i < numAlgos; ++i)
            if (s.weights[i] > 0.0f)
            {
                mean += s.weights[i];
                ++n;
            }

        mean = n > 0 ? mean / (float) n : 0.0f;
    }
    else
    {
        const auto& o = *overlayFor (p);
        std::copy (std::begin (o.weight), std::end (o.weight), std::begin (wMul));
        density = o.density; complexity = o.complexity; chaos = o.chaos; intensity = o.intensity;
    }

    // Weight overlay: bypassed by a forced Fill Type; a zero weight stays zero; if every
    // effective weight ends up zero the user's weights stand for the block.
    if (s.fillTypeIndex == 0)
    {
        float out[numAlgos];
        bool any = false;

        for (int i = 0; i < numAlgos; ++i)
        {
            const auto user = s.weights[i];

            if (user <= 0.0f)
                out[i] = 0.0f;
            else if (unhinged)
                out[i] = clamp01 (user + (mean - user) * kUnhingedFlatten);
            else
                out[i] = clamp01 (user * wMul[i]);

            any = any || out[i] > 0.0f;
        }

        if (any)
            std::copy (std::begin (out), std::end (out), std::begin (s.weights));
    }

    s.density01    = clamp01 (s.density01 * density);
    s.complexity01 = clamp01 (s.complexity01 * complexity);
    s.chaos01      = clamp01 (s.chaos01 * chaos);
    s.intensity01  = clamp01 (s.intensity01 * intensity);

    if (unhinged)
        s.perf.mistakes01 = std::max (s.perf.mistakes01, kUnhingedMistakes01);
}

} // namespace gapfill::personality
