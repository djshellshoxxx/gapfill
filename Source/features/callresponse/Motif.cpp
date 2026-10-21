#include "Motif.h"

#include <algorithm>
#include <cmath>

namespace gapfill::callresponse
{

Motif describe (const CallEntry* calls, int n) noexcept
{
    Motif m;
    n = std::clamp (n, 0, kMaxCalls);

    if (n == 0)
        return m;

    int familyCount[4] {};
    int bandCount[4] {};            // index 0 = broadband, 1..3 = low / mid / high
    int rise = 0, fall = 0;
    double lengthSum = 0.0;
    bool allBeats = true;

    for (int i = 0; i < n; ++i)
    {
        const auto& c = calls[i];
        const int algo = std::min<int> (c.algo, numAlgos - 1);
        ++familyCount[(int) kAlgoFamilies[(size_t) algo]];
        ++bandCount[c.band < 0 || c.band > 2 ? 0 : c.band + 1];
        rise += algo == (int) Algo::PitchRise;
        fall += algo == (int) Algo::PitchFall;
        lengthSum += c.lengthClass;
        allBeats = allBeats && c.hasBeat;
    }

    int bestFamily = 0;

    for (int f = 1; f < 4; ++f)
        if (familyCount[f] > familyCount[bestFamily])
            bestFamily = f;                                  // ties: lowest enum value

    m.family = (AlgoFamily) bestFamily;
    m.familyShare = (float) familyCount[bestFamily] / (float) n;
    m.meanLengthClass = (std::uint8_t) std::clamp ((int) std::lround (lengthSum / n), 0, 3);
    m.pitchDir = (std::int8_t) (rise > fall ? 1 : (fall > rise ? -1 : 0));

    int bestBand = 0;                                        // ties: broadband

    for (int b = 1; b < 4; ++b)
        if (bandCount[b] > bandCount[bestBand])
            bestBand = b;

    m.band = (std::int8_t) (bestBand - 1);

    if (n >= 3)
    {
        double d[kMaxCalls - 1];
        double mean = 0.0;

        for (int i = 1; i < n; ++i)
        {
            d[i - 1] = allBeats ? calls[i].startBeat - calls[i - 1].startBeat
                                : calls[i].startSeconds - calls[i - 1].startSeconds;
            mean += d[i - 1];
        }

        mean /= (n - 1);
        double var = 0.0;

        for (int i = 0; i < n - 1; ++i)
            var += (d[i] - mean) * (d[i] - mean);

        const double sd = std::sqrt (var / (n - 1));
        const double cv = mean > 1.0e-9 ? sd / mean : 1.0;
        m.regularity = (float) (1.0 - std::clamp (cv / 0.5, 0.0, 1.0));
    }
    else
        m.regularity = 0.5f;

    m.confidence = 0.6f * m.familyShare + 0.4f * m.regularity;

    // A motif needs a dominant family: without one, regular spacing alone must not open a response.
    if (m.familyShare < 0.5f)
        m.confidence = std::min (m.confidence, kMotifGate - 0.01f);
    return m;
}

} // namespace gapfill::callresponse
