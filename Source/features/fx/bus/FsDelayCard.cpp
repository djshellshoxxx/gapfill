#include "FsDelayCard.h"

#include "../../../Parameters.h"

namespace gapfill::fx::bus
{

namespace
{
    double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;

        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }

        return sum;
    }
}

void FsDelayCard::prepareCard()
{
    for (auto& c : ch)
        c.line.allocate (maxDelaySamples() + 64);

    // 63-tap Kaiser-windowed Hilbert transformer
    constexpr double beta = 7.0;
    const auto i0b = besselI0 (beta);

    for (int n = 0; n < kHilbertTaps; ++n)
    {
        const int m = n - kHilbertDelay;
        double h = 0.0;

        if ((m & 1) != 0)
            h = 2.0 / (3.14159265358979323846 * (double) m);

        const double r = (double) m / (double) kHilbertDelay;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        hilbertTap[n] = (float) (h * w);
    }

    slewK = 1.0f - std::exp (-1.0f / (0.0167f * (float) sr));
}

void FsDelayCard::resetCard() noexcept
{
    for (auto& c : ch)
    {
        c.line.clear();

        for (auto& p : c.lp)
            p.reset();

        c.dc.reset();
        std::fill (std::begin (c.hist), std::end (c.hist), 0.0f);
        c.hw = 0;
        c.cs = 1.0f;
        c.sn = 0.0f;
        c.sinceNorm = 0;
        c.primed = false;
    }
}

void FsDelayCard::blockSettings (const FxSettings& fs, const BusFxContext& ctx) noexcept
{
    const auto& s = fs.fsdelay;

    fb = std::min (kMaxFeedback, s.feedback01);
    dampA = onePoleCoeff (16000.0f + (2500.0f - 16000.0f) * s.damp01, sr);

    const auto timeSamples = divisionSamples (divisionInBeats (s.time), ctx.samplesPerBeat, sr, kFallbackBpm);
    const auto maxD = (double) maxDelaySamples();
    const auto base = std::clamp (timeSamples, (double) kMinLoopDelay, maxD);

    shiftHz[0] = s.shiftHz;
    shiftHz[1] = s.shiftHz * (1.0f - 2.0f * s.stereo01);

    for (int c = 0; c < 2; ++c)
    {
        const auto t = c == 0 ? base : std::min (maxD, base * (1.0 + 0.25 * s.stereo01));
        ch[c].delayTarget = (float) t;
        shiftOn[c] = std::abs (shiftHz[c]) > 0.01f;

        const auto w = kTwoPi * shiftHz[c] / (float) sr;
        ch[c].cd = std::cos (w);
        ch[c].sd = std::sin (w);

        if (! ch[c].primed)
        {
            ch[c].delayCur = ch[c].delayTarget;
            ch[c].primed = true;
        }
    }
}

void FsDelayCard::renderChunk (float* l, float* r, int n, const float* amt, const float* duck,
                               const BusFxContext&, const FxSettings&) noexcept
{
    float* data[2] { l, r };

    for (int i = 0; i < n; ++i)
    {
        const auto k = duck != nullptr ? duck[i] : 1.0f;

        for (int c = 0; c < 2; ++c)
        {
            auto& st = ch[c];
            st.delayCur += (st.delayTarget - st.delayCur) * slewK;

            const bool shifting = shiftOn[c];
            const auto readDelay = shifting ? std::max (2.0f, st.delayCur - (float) kHilbertDelay) : st.delayCur;

            auto v = st.line.read (readDelay);

            for (auto& p : st.lp)
                v = p.lp (v, dampA);

            float echo = v;

            if (shifting)
            {
                st.hist[st.hw] = v;

                float q = 0.0f;

                for (int t = 0; t < kHilbertTaps; t += 2)          // odd offsets from the centre only
                    q += hilbertTap[t] * st.hist[(st.hw - t) & 63];

                const auto inPhase = st.hist[(st.hw - kHilbertDelay) & 63];
                st.hw = (st.hw + 1) & 63;

                echo = inPhase * st.cs - q * st.sn;

                const auto ncs = st.cs * st.cd - st.sn * st.sd;
                const auto nsn = st.sn * st.cd + st.cs * st.sd;
                st.cs = ncs;
                st.sn = nsn;

                if (++st.sinceNorm >= 256)
                {
                    const auto inv = 1.0f / std::sqrt (st.cs * st.cs + st.sn * st.sn);
                    st.cs *= inv;
                    st.sn *= inv;
                    st.sinceNorm = 0;
                }
            }

            echo = st.dc.process (echo);
            st.line.push (flush (data[c][i] + fb * echo));
            data[c][i] += amt[i] * echo * k;
        }
    }
}

} // namespace gapfill::fx::bus
