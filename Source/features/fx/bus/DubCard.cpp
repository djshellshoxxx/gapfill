#include "DubCard.h"

#include "../../../Parameters.h"

namespace gapfill::fx::bus
{

void DubCard::prepareCard()
{
    for (auto& c : ch)
        c.line.allocate ((int) (2.0 * sr) + 64);

    slewK = 1.0f - std::exp (-1.0f / (0.0167f * (float) sr));
    primed = false;
}

void DubCard::resetCard() noexcept
{
    for (auto& c : ch)
    {
        c.line.clear();
        c.hp.reset();
        c.lp.reset();
        c.dark.reset();
        c.dc.reset();
    }

    lfoPhase = 0.0;
    cutoffSmooth = highHz;
    sinceUpdate = 0;
    primed = false;
}

void DubCard::blockSettings (const FxSettings& fs, const BusFxContext& ctx) noexcept
{
    const auto& s = fs.dub;

    fb = std::min (kMaxFeedback, s.feedback01);
    drive = 1.0f + 3.0f * s.sat01;
    lowHz = s.lowHz;
    highHz = std::max (s.highHz, s.lowHz * 1.5f);
    move = s.move01;
    ping = s.ping;

    const auto t = std::clamp (divisionSamples (divisionInBeats (s.div), ctx.samplesPerBeat, sr, kFallbackBpm),
                               64.0, 2.0 * sr);
    delayTarget[0] = delayTarget[1] = (float) t;

    if (! primed)
    {
        delayCur[0] = delayCur[1] = (float) t;
        cutoffSmooth = highHz;
        primed = true;
        updateFilters();
    }

    lfoPeriod = ctx.samplesPerBeat > 1.0 ? 8.0 * ctx.samplesPerBeat : 4.0 * sr;
}

void DubCard::updateFilters() noexcept
{
    for (auto& c : ch)
    {
        c.hp.set (lowHz, 0.7071f, sr);
        c.lp.set (cutoffSmooth, 0.7071f, sr);
    }

    darkA = onePoleCoeff (std::min (0.9f * 3.0f * cutoffSmooth, 0.45f * (float) sr), sr);
}

void DubCard::renderChunk (float* l, float* r, int n, const float* amt, const float* duck,
                           const BusFxContext&, const FxSettings&) noexcept
{
    float* data[2] { l, r };

    for (int i = 0; i < n; ++i)
    {
        if (sinceUpdate == 0)
        {
            const auto lfo = lfoShape ((float) (lfoPhase / lfoPeriod));
            const auto target = std::clamp (highHz * std::exp2 (1.5f * move * lfo), lowHz * 1.5f, 0.45f * (float) sr);
            cutoffSmooth += (target - cutoffSmooth) * 0.2f;
            updateFilters();
        }

        sinceUpdate = (sinceUpdate + 1) & 15;
        lfoPhase += 1.0;

        if (lfoPhase >= lfoPeriod)
            lfoPhase -= lfoPeriod;

        const auto k = duck != nullptr ? duck[i] : 1.0f;
        float echo[2];

        for (int c = 0; c < 2; ++c)
        {
            delayCur[c] += (delayTarget[c] - delayCur[c]) * slewK;
            auto& st = ch[c];
            auto v = st.line.read (delayCur[c]);

            float lo, hi, unused;
            st.hp.process (v, unused, hi);              // high-pass
            st.lp.process (hi, lo, unused);             // low-pass
            v = st.dark.lp (lo, darkA);                 // repeat darkening
            v = std::tanh (drive * v) / drive;          // saturator, small-signal gain 1
            echo[c] = st.dc.process (v);
        }

        const auto inL = data[0][i], inR = data[1][i];

        if (ping)
        {
            // mono input feeds the left line; each line feeds the other
            ch[0].line.push (flush (0.5f * (inL + inR) + fb * echo[1]));
            ch[1].line.push (flush (fb * echo[0]));
        }
        else
        {
            ch[0].line.push (flush (inL + fb * echo[0]));
            ch[1].line.push (flush (inR + fb * echo[1]));
        }

        data[0][i] = inL + softClamp (amt[i] * echo[0] * k);
        data[1][i] = inR + softClamp (amt[i] * echo[1] * k);
    }
}

} // namespace gapfill::fx::bus
