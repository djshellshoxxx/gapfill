#include "RatchetCard.h"

#include "../Salts.h"
#include "../../Parameters.h"
#include "../../dsp/DspUtils.h"

#include <algorithm>
#include <cmath>

namespace gapfill::fx
{

RatchetCard::Plan RatchetCard::makePlan (const RatchetSettings& s, double samplesPerBeat,
                                         double sampleRate, int voiceLength) noexcept
{
    Plan p;

    if (voiceLength <= 0 || sampleRate <= 0.0)
        return p;

    double spb = samplesPerBeat;

    if (spb < 1.0)
    {
        spb = sampleRate * 60.0 / kFallbackBpm;
        p.tempoFallback = true;
    }

    const auto ringCap = (int) std::ceil (sampleRate * 0.001 * (double) kRingMs);
    const auto minInterval = (int) std::lround (sampleRate * 0.001 * (double) kMinIntervalMs);

    auto slice = (int) std::lround (spb * divisionInBeats (s.division));
    slice = std::max (1, std::min ({ slice, ringCap, voiceLength }));

    const auto repeats = std::max (1, std::min (kMaxRepeats, s.repeats));
    const auto factor = 1.0 - 0.5 * (double) juce::jlimit (-1.0f, 1.0f, s.accel);
    const auto gate = (double) juce::jlimit (0.1f, 1.0f, s.gate01);

    double interval = (double) slice;
    double t = 0.0;
    int count = 0;

    for (int k = 0; k < repeats; ++k)
    {
        const auto start = (int) std::llround (t);

        // No repeat begins with fewer than 8 ms of voice left (the first always may).
        if (k > 0 && voiceLength - start < minInterval)
            break;

        const auto iv = (int) std::llround (interval);
        const auto gateLen = std::max (1, (int) std::llround (gate * (double) iv));

        p.start[k] = start;
        p.interval[k] = iv;
        p.playLength[k] = std::max (1, std::min ({ slice, gateLen, voiceLength - start }));
        ++count;

        t += interval;
        interval = std::max ((double) minInterval, interval * factor);
    }

    p.sliceLength = slice;
    p.count = count;
    p.active = count > 0;
    return p;
}

void RatchetCard::prepare (double sampleRate, int /*maxBlock*/, int /*channels*/)
{
    sr = sampleRate;
    ringSize = (int) std::ceil (sr * 0.001 * (double) kRingMs) + 4;
    ringL.assign ((size_t) ringSize, 0.0f);
    ringR.assign ((size_t) ringSize, 0.0f);
    edge = std::max (1, (int) std::lround (sr * 0.001 * (double) kEdgeMs));
    reset();
}

void RatchetCard::reset()
{
    plan = {};
    pos = 0;
    cursor = 0;
    std::fill (ringL.begin(), ringL.end(), 0.0f);
    std::fill (ringR.begin(), ringR.end(), 0.0f);
}

bool RatchetCard::startFill (const VoiceFxStart& st)
{
    plan = {};
    pos = 0;
    cursor = 0;

    const auto& s = st.settings.ratchet;

    if (s.amount01 <= kOffThreshold)
        return false;

    Rng sub;
    sub.seed (hashCombine (st.seed, kSalt_Ratchet));

    if (! sub.chance (s.amount01))
        return false;

    plan = makePlan (s, st.samplesPerBeat, sr, st.lengthSamples);
    plan.tempoFallback = plan.tempoFallback || st.tempoFallback;
    return plan.active;
}

void RatchetCard::processVoice (float* l, float* r, int n, const VoiceFxContext&) noexcept
{
    noteCall();

    if (! plan.active)
        return;

    for (int i = 0; i < n; ++i, ++pos)
    {
        const auto inL = l[i];
        const auto inR = r[i];

        if (pos < plan.sliceLength && pos < ringSize)
        {
            ringL[(size_t) pos] = inL;
            ringR[(size_t) pos] = inR;
        }

        // Move to the latest repeat that has started.
        while (cursor + 1 < plan.count && pos >= plan.start[cursor + 1])
            ++cursor;

        const auto k = cursor;
        const auto local = pos - plan.start[k];
        const auto len = plan.playLength[k];

        float outL = 0.0f, outR = 0.0f;

        if (local >= 0 && local < len)
        {
            const auto e = std::min (edge, std::max (1, len / 2));
            float g = 1.0f;

            if (local < e)
                g = hann (0.5f * (float) local / (float) e);

            const auto fromEnd = len - 1 - local;

            if (fromEnd < e)
                g = std::min (g, hann (0.5f * (float) fromEnd / (float) e));

            if (k == 0)
            {
                outL = inL * g;
                outR = inR * g;
            }
            else
            {
                outL = ringL[(size_t) local] * g;
                outR = ringR[(size_t) local] * g;
            }
        }

        l[i] = outL;
        r[i] = outR;
    }
}

} // namespace gapfill::fx
