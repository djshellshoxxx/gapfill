#include "Describe.h"
#include "../lock/LikeBias.h"

namespace gapfill::history
{

const DescribeStrings& strings() noexcept
{
    static const DescribeStrings en { { "Low", "Mid", "High" }, "ms", " - ", "Locked", "Lock armed" };
    return en;
}

juce::String describeRecipe (const FillRecipe& r, double /*bpm*/)
{
    const auto& t = strings();
    juce::String text (algoName ((Algo) juce::jlimit (0, numAlgos - 1, (int) r.algo)));

    if (r.division != FillRecipe::noDivision && r.division < (int) Division::NumDivisions)
        text << " " << divisionNames()[(int) r.division];

    if (r.band >= 0 && r.band <= 2)
        text << t.separator << t.bands[r.band];

    text << t.separator << juce::String ((int) r.lengthMs) << " " << t.millis;
    return text;
}

char familyLetter (int algo) noexcept
{
    if (algo < 0 || algo >= numAlgos)
        return '?';

    switch (kAlgoFamilies[(size_t) algo])
    {
        case AlgoFamily::Rhythmic:    return 'R';
        case AlgoFamily::Textural:    return 'T';
        case AlgoFamily::PitchTape:   return 'P';
        case AlgoFamily::Destructive: return 'D';
    }

    return '?';
}

std::uint8_t recipeDivision (int algo, const EngineSettings& s, double sampleRate) noexcept
{
    double seconds = 0.0;

    if (algo == (int) Algo::Stutter)
        seconds = s.stutterSeconds;
    else if (algo == (int) Algo::Delay)
        seconds = s.delaySeconds;
    else
        return FillRecipe::noDivision;

    // Same fallback as the processor: 120 bpm when the host gives no tempo.
    const auto beatSeconds = s.samplesPerBeat > 1.0 && sampleRate > 0.0 ? s.samplesPerBeat / sampleRate : 0.5;

    int best = -1;
    double bestError = 0.03;               // accept within 3 %

    for (int d = 0; d < (int) Division::NumDivisions; ++d)
    {
        const auto length = divisionInBeats (d) * beatSeconds;

        if (length <= 0.0)
            continue;

        const auto error = std::abs (seconds / length - 1.0);

        if (error < bestError)
        {
            best = d;
            bestError = error;
        }
    }

    return best >= 0 ? (std::uint8_t) best : FillRecipe::noDivision;
}

} // namespace gapfill::history
