#include "HarmonicGrains.h"

#include "../Salts.h"

#include <algorithm>
#include <cmath>

namespace gapfill::harmonic
{

float intervalRatio (int index, bool minorThird) noexcept
{
    switch (index)
    {
        case 0:  return 1.0f;
        case 1:  return 2.0f;
        case 2:  return 1.5f;
        case 3:  return minorThird ? 1.2f : 1.25f;
        case 4:  return 0.5f;
        default: return 4.0f;
    }
}

void HarmonicGrains::reset() noexcept
{
    on = false;
    cfg = {};
    numStreams = 0;
    elapsed = 0;
    toneLp[0] = toneLp[1] = 0.0f;

    for (auto& s : streams)
        s = {};
}

void HarmonicGrains::configure (const HarmonicSetup& setup, std::int64_t anchor, double sampleRate, std::uint64_t voiceSeed) noexcept
{
    reset();

    if (! setup.enabled || setup.f0Hz < 30.0f || setup.f0Hz > 1100.0f || sampleRate < 8000.0)
        return;

    cfg = setup;
    sr = sampleRate;
    anchorPos = anchor;
    t0 = sr / (double) setup.f0Hz;
    rng.seed (hashCombine (voiceSeed, kSalt_HGran));

    // Grain length: an even number of periods (50 % overlap then keeps neighbours in phase), 20..120 ms.
    periodsPerGrain = t0 * 4.0 >= 0.020 * sr ? 4 : 6;

    while (periodsPerGrain > 2 && t0 * periodsPerGrain > 0.120 * sr)
        periodsPerGrain -= 2;

    grainSourceLen = std::max (16, (int) std::lround (t0 * periodsPerGrain));
    maxBackPeriods = std::max (0, (int) std::floor ((0.30 * sr - (double) grainSourceLen) / t0));

    lfoHz = 0.05f + 0.45f * std::clamp (setup.motion01, 0.0f, 1.0f);
    toneCoef = 1.0f - (float) std::exp (-6.2831853 * 4000.0 / sr);

    numStreams = std::clamp (setup.voices, 1, kMaxStreams);
    static const float pans[kMaxStreams] = { 0.0f, -0.3f, 0.3f, -0.15f, 0.15f, 0.0f };

    for (int i = 0; i < numStreams; ++i)
    {
        auto& st = streams[(size_t) i];
        st.ratio = (double) intervalRatio (i, setup.minorThird);
        st.lfoPhase = rng.nextFloat();
        const float p = 0.5f + 0.5f * pans[i];                          // equal-power pan
        st.panL = std::sqrt (1.0f - p) * 1.4142f;
        st.panR = std::sqrt (p) * 1.4142f;
        st.countdown = (double) i * 0.37 * (double) grainSourceLen / st.ratio * 0.25;     // stagger the streams
    }

    on = true;
}

void HarmonicGrains::spawn (Stream& st, int streamIndex) noexcept
{
    const float motion = std::clamp (cfg.motion01, 0.0f, 1.0f);
    const double t = (double) elapsed / sr;
    const double lfo = std::sin (6.283185307 * ((double) lfoHz * t + (double) st.lfoPhase));

    // Whole periods back from the anchor, never closer than one grain length.
    const int travel = (int) std::lround ((double) motion * (double) maxBackPeriods * (0.5 + 0.5 * lfo));
    const double back = (double) grainSourceLen + (double) travel * t0;

    const double cents = 15.0 * (double) motion * lfo * (streamIndex % 2 == 0 ? 1.0 : -1.0);
    const double rate = st.ratio * std::pow (2.0, cents / 1200.0);

    for (auto& g : st.grains)
    {
        if (g.active)
            continue;

        g.active = true;
        g.pos = (double) anchorPos - back;
        g.rate = rate;
        g.age = 0;
        g.length = std::max (8, (int) std::lround ((double) grainSourceLen / rate));
        return;
    }
}

void HarmonicGrains::render (float& l, float& r, const SourceHistory& history, int channels) noexcept
{
    if (! on)
        return;

    float accL = 0.0f, accR = 0.0f;

    for (int si = 0; si < numStreams; ++si)
    {
        auto& st = streams[(size_t) si];

        st.countdown -= 1.0;

        if (st.countdown <= 0.0)
        {
            spawn (st, si);
            st.countdown += 0.5 * (double) grainSourceLen / st.ratio;            // 50 % overlap in output time
        }

        for (auto& g : st.grains)
        {
            if (! g.active)
                continue;

            const float w = hann ((float) g.age / (float) g.length);
            const float a = history.read (0, g.pos);
            const float b = channels > 1 ? history.read (1, g.pos) : a;

            accL += a * w * st.panL;
            accR += b * w * st.panR;
            g.pos += g.rate;

            if (++g.age >= g.length)
                g.active = false;
        }
    }

    const float norm = 0.9f / std::sqrt ((float) numStreams);
    accL *= norm;
    accR *= norm;

    if (std::abs (cfg.tone) > 1.0e-4f)
    {
        toneLp[0] += toneCoef * (accL - toneLp[0]);
        toneLp[1] += toneCoef * (accR - toneLp[1]);
        accL += cfg.tone * (accL - toneLp[0]);
        accR += cfg.tone * (accR - toneLp[1]);
    }

    l += std::clamp (accL, -2.0f, 2.0f);
    r += std::clamp (accR, -2.0f, 2.0f);
    ++elapsed;
}

int HarmonicGrains::activeGrains() const noexcept
{
    int n = 0;

    for (int i = 0; i < numStreams; ++i)
        for (const auto& g : streams[(size_t) i].grains)
            n += g.active;

    return n;
}

} // namespace gapfill::harmonic
