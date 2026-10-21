#include "CloudMode.h"

#include "../Salts.h"

#include <algorithm>
#include <cmath>

namespace gapfill::fx
{

CloudDensity limitCloudDensity (float densityHz, float sizeMs) noexcept
{
    CloudDensity d;
    const auto size = std::max (1.0f, sizeMs);
    d.effective = std::max (0.0f, densityHz);
    d.overlap = d.effective * size * 0.001f;

    if (d.overlap > kCloudMaxOverlap)
    {
        d.effective = kCloudMaxOverlap * 1000.0f / size;
        d.overlap = kCloudMaxOverlap;
        d.limited = true;
    }

    return d;
}

void CloudMode::reset() noexcept
{
    active = false;

    for (auto& g : grains)
        g.active = false;
}

bool CloudMode::configure (bool requested, const CloudSettings& settings, float chaos01,
                           std::int64_t anchorPos, std::uint64_t voiceSeed, double sampleRate) noexcept
{
    reset();
    dropped = 0;

    if (! requested)
        return false;

    s = settings;
    grainCap = std::clamp (settings.maxGrains, 1, kCloudMaxGrains);      // F-D03
    chaos = juce::jlimit (0.0f, 1.0f, chaos01);
    anchor = anchorPos;
    sr = sampleRate;
    rng.seed (hashCombine (voiceSeed, kSalt_Cloud));

    dens = limitCloudDensity (s.densityHz, s.sizeMs);
    norm = 1.0f / std::sqrt (std::max (1.0f, dens.overlap));
    countdown = 0;
    active = dens.effective > 0.0f;
    return active;
}

int CloudMode::activeGrains() const noexcept
{
    int n = 0;

    for (const auto& g : grains)
        if (g.active)
            ++n;

    return n;
}

CloudGrain CloudMode::drawGrain (std::int64_t oldest) noexcept
{
    CloudGrain g;

    const auto sizeScale = 1.0f + 0.4f * (rng.nextFloat() - 0.5f);                 // +-20 %
    g.dur = std::max (16, (int) std::lround (sr * 0.001 * (double) s.sizeMs * (double) sizeScale));
    g.tukey = s.sizeMs < 20.0f;

    // Pitch: triangular within +-cents, optionally snapped to the scale.
    auto cents = (rng.nextFloat() + rng.nextFloat() - 1.0f) * s.pitchCents;

    if (quantiser != nullptr)
        cents = quantiser (cents, quantiserUser);

    g.rate = std::pow (2.0, (double) cents / 1200.0);

    // Position: samples before the anchor.
    const auto u = rng.nextFloat();
    const auto spanSamples = (double) s.spanMs * 0.001 * sr;
    const auto back = spanSamples * (0.5 + (double) s.jitter01 * ((double) u - 0.5));

    const auto grainSpan = (double) g.dur * g.rate;
    auto start = (double) anchor - back;
    start = std::min (start, (double) anchor - grainSpan);           // never beyond the anchor
    start = std::max (start, (double) oldest);                       // never before valid history

    const bool reverse = rng.chance (chaos * 0.3f);

    if (reverse)
    {
        g.pos = start + grainSpan;
        g.rate = -g.rate;
    }
    else
    {
        g.pos = start;
    }

    const auto pan = rng.nextBipolar() * s.spread01;
    const auto angle = (pan * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
    g.panL = std::cos (angle) * 1.41421356f;
    g.panR = std::sin (angle) * 1.41421356f;
    g.amp = rng.range (0.8f, 1.0f);
    g.age = 0;
    g.active = true;
    return g;
}

bool CloudMode::generate (float& l, float& r, const SourceHistory& history, int channels) noexcept
{
    if (! active)
        return false;

    if (--countdown <= 0)
    {
        const auto oldest = history.getWritePosition() - (std::int64_t) history.getSize() + 2;
        const auto g = drawGrain (std::max ((std::int64_t) 0, oldest));
        bool placed = false;

        for (int i = 0; i < grainCap; ++i)
        {
            auto& slot = grains[i];

            if (! slot.active)
            {
                slot = g;
                placed = true;
                break;
            }
        }

        if (! placed)
            ++dropped;

        const auto mean = sr / (double) dens.effective;
        const auto jitter = 1.0 + 0.5 * (2.0 * (double) rng.nextFloat() - 1.0);     // +-50 %
        countdown = std::max (1, (int) std::lround (mean * jitter));
    }

    float accL = 0.0f, accR = 0.0f;

    for (auto& g : grains)
    {
        if (! g.active)
            continue;

        const auto phase = (float) g.age / (float) g.dur;
        float w;

        if (g.tukey)
        {
            constexpr float alpha = 0.5f;
            const auto edge = alpha * 0.5f;
            w = phase < edge ? hann (0.5f * phase / edge)
                             : (phase > 1.0f - edge ? hann (0.5f * (1.0f - phase) / edge) : 1.0f);
        }
        else
        {
            w = hann (phase);
        }

        const auto s0 = history.read (0, g.pos);
        const auto s1 = history.read (channels > 1 ? 1 : 0, g.pos);

        accL += s0 * w * g.amp * g.panL;
        accR += s1 * w * g.amp * g.panR;

        g.pos += g.rate;

        if (++g.age >= g.dur)
            g.active = false;
    }

    l = accL * norm;
    r = accR * norm;
    return true;
}

} // namespace gapfill::fx
