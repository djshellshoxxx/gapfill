#include "ResonatorCard.h"

#include "../../Salts.h"
#include "../../../dsp/DspUtils.h"

namespace gapfill::fx::bus
{

int ResonatorCard::pickScaleNote (int centre, std::uint16_t scaleMask, int scaleRoot, std::uint32_t seed) noexcept
{
    centre = std::clamp (centre, 24, 96);

    if (scaleMask == 0)
        return centre;

    int candidates[25];
    int count = 0;

    for (int n = std::max (24, centre - 12); n <= std::min (96, centre + 12); ++n)
    {
        const auto rel = ((n - scaleRoot) % 12 + 12) % 12;

        if ((scaleMask >> rel) & 1)
            candidates[count++] = n;
    }

    if (count == 0)
        return centre;

    Rng sub;
    sub.seed (hashCombine ((std::uint64_t) seed, kSalt_Resonator));
    return candidates[sub.nextInt (count)];
}

void ResonatorCard::prepareCard()
{
    // lowest note 24 (32.7 Hz) minus the largest detune
    const auto maxDelay = (int) (sr / 30.0) + 16;

    for (auto& ch : comb)
        for (auto& c : ch)
            c.line.allocate (maxDelay);

    xfLen = std::max (1, (int) (0.010 * sr));
}

void ResonatorCard::resetCard() noexcept
{
    for (auto& ch : comb)
        for (auto& c : ch)
        {
            c.line.clear();
            c.damp.reset();
            c.dc.reset();
        }

    inHp[0].reset();
    inHp[1].reset();
    xf = 1.0f;
    crossfading = false;
    active[0] = active[1] = 0;
    lastRequested = -1.0f;
    lastSpread = lastDecay = lastDamp = -1.0f;
}

void ResonatorCard::tuneComb (Comb& c, float hz, const ResonatorSettings& s) const noexcept
{
    hz = std::clamp (hz, 20.0f, 0.45f * (float) sr);
    const auto fc = 18000.0f + (1200.0f - 18000.0f) * s.damp01;
    c.dampA = onePoleCoeff (fc, sr);

    const auto w = kTwoPi * hz / (float) sr;
    const auto d = (float) sr / hz - onePolePhaseDelay (c.dampA, w) - dcBlockPhaseDelay (kDcR, w);
    c.delay = std::max (2.0f, d);

    const auto rt60 = 0.05f * std::pow (40.0f, s.decay01);
    c.g = std::min (kMaxFeedback, std::pow (10.0f, -3.0f * (float) (sr / hz) / ((float) sr * rt60)));
}

void ResonatorCard::retune (float noteNow, const ResonatorSettings& s) noexcept
{
    const auto cents = 15.0f * s.spread01;
    const auto hzL = noteToHz (noteNow - cents / 100.0f);
    const auto hzR = noteToHz (noteNow + cents / 100.0f);

    if (lastRequested < 0.0f)
    {
        // first tuning: both combs of both channels straight to the target
        for (int b = 0; b < 2; ++b)
        {
            tuneComb (comb[0][b], hzL, s);
            tuneComb (comb[1][b], hzR, s);
        }

        crossfading = false;
        xf = 1.0f;
    }
    else
    {
        const int other0 = 1 - active[0], other1 = 1 - active[1];
        // the incoming comb starts empty: re-tuning a ringing delay line would click
        for (auto* c : { &comb[0][other0], &comb[1][other1] })
        {
            c->line.clear();
            c->damp.reset();
            c->dc.reset();
        }

        tuneComb (comb[0][other0], hzL, s);
        tuneComb (comb[1][other1], hzR, s);
        crossfading = true;
        xf = 0.0f;
    }

    note = noteNow;
    lastRequested = noteNow;
    lastSpread = s.spread01;
    lastDecay = s.decay01;
    lastDamp = s.damp01;
    comp = 1.0f;
}

void ResonatorCard::blockSettings (const FxSettings& fs, const BusFxContext& ctx) noexcept
{
    const auto& s = fs.resonator;

    float wanted = (float) s.note;

    if (s.mode == 1 && ctx.scaleMask != 0)
    {
        wanted = note;                                   // keep the last pick between fills

        if (lastRequested < 0.0f || lastSpread < 0.0f)
            wanted = (float) pickScaleNote (s.note, ctx.scaleMask, ctx.scaleRoot, 0u);

        for (int i = 0; i < ctx.numStarts; ++i)
            wanted = (float) pickScaleNote (s.note, ctx.scaleMask, ctx.scaleRoot, ctx.fillStarts[i].recipe.seed);
    }

    if (wanted != lastRequested || s.spread01 != lastSpread)
    {
        retune (wanted, s);
    }
    else if (s.decay01 != lastDecay || s.damp01 != lastDamp)
    {
        const auto cents = 15.0f * s.spread01;

        for (int b = 0; b < 2; ++b)
        {
            tuneComb (comb[0][b], noteToHz (note - cents / 100.0f), s);
            tuneComb (comb[1][b], noteToHz (note + cents / 100.0f), s);
        }

        lastDecay = s.decay01;
        lastDamp = s.damp01;
    }

    // gain compensation sqrt (1 - g) of the active comb
    comp = std::sqrt (std::max (0.02f, 1.0f - comb[0][active[0]].g));
}

void ResonatorCard::renderChunk (float* l, float* r, int n, const float* amt, const float* duck,
                                 const BusFxContext&, const FxSettings&) noexcept
{
    float* data[2] { l, r };
    const auto hpA = onePoleCoeff (40.0f, sr);

    for (int i = 0; i < n; ++i)
    {
        const auto fade = crossfading ? xf : 1.0f;
        const auto k = duck != nullptr ? duck[i] : 1.0f;

        for (int c = 0; c < 2; ++c)
        {
            const auto in = data[c][i];
            const auto x = in - inHp[c].lp (in, hpA);               // 40 Hz high-pass

            const auto yCur = comb[c][active[c]].process (x);
            const auto yNxt = comb[c][1 - active[c]].process (x);
            const auto y = crossfading ? yCur * (1.0f - fade) + yNxt * fade : yCur;

            data[c][i] = in + amt[i] * comp * y * k;
        }

        if (crossfading)
        {
            xf += 1.0f / (float) xfLen;

            if (xf >= 1.0f)
            {
                xf = 1.0f;
                crossfading = false;
                active[0] = 1 - active[0];
                active[1] = 1 - active[1];
            }
        }
    }
}

} // namespace gapfill::fx::bus
