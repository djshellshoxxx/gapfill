#include "Suggest.h"

#include "../../Presets.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gapfill::suggest
{

const std::vector<FamilyRow>& familyTable()
{
    // events/s, gap fraction, level dB, low share, high share. Starting values from the spec, frozen by test.
    static const std::vector<FamilyRow> table =
    {
        { "Subtle Ear Candy",      3.0f, 0.35f, -18.0f, 0.35f, 0.25f, "Subtle" },
        { "Drum Gap Glitch",       5.0f, 0.30f, -14.0f, 0.40f, 0.25f, "Glitch" },
        { "DnB Micro Fills",       9.0f, 0.20f, -12.0f, 0.45f, 0.25f, "Rhythmic" },
        { "Breakcore Destroyer",  12.0f, 0.15f, -10.0f, 0.35f, 0.35f, "Destructive" },
        { "Ghost Reverses",        2.0f, 0.45f, -20.0f, 0.30f, 0.25f, "Ambient" },
        { "Industrial Debris",     6.0f, 0.25f, -12.0f, 0.35f, 0.30f, "Destructive" },
        { "Vocal Ghosts",          1.5f, 0.45f, -20.0f, 0.15f, 0.35f, "Ambient" },
        { "Vocal Delay Fills",     2.5f, 0.40f, -18.0f, 0.15f, 0.30f, "Dub" },
        { "Bass Between Kicks",    3.5f, 0.35f, -14.0f, 0.60f, 0.10f, "Utility" },
        { "Granular Spaces",       1.0f, 0.55f, -24.0f, 0.25f, 0.30f, "Ambient" },
        { "Trance Transitions",    8.0f, 0.18f, -10.0f, 0.40f, 0.30f, "Rhythmic" },
        { "Trip Hop Dust",         2.5f, 0.45f, -18.0f, 0.45f, 0.20f, "Subtle" },
        { "Spectral High Fill",    4.0f, 0.30f, -16.0f, 0.20f, 0.50f, "Glitch" },
        { "Spectral Mid Mutation", 4.0f, 0.30f, -16.0f, 0.20f, 0.20f, "Glitch" },
        { "Sparse Texture",        0.8f, 0.60f, -26.0f, 0.30f, 0.25f, "Ambient" },
        { "Extreme Chaos",        10.0f, 0.20f,  -8.0f, 0.35f, 0.35f, "Destructive" },
        // U-10 passes: one row per later factory preset (starting values; the archetype test freezes the ranking).
        { "Breaks Chop Shop",           7.0f, 0.22f, -12.0f, 0.40f, 0.30f, "Rhythmic" },
        { "Dub Echo Throwbacks",        2.5f, 0.42f, -18.0f, 0.35f, 0.20f, "Dub" },
        { "Trance Riser Fills",         6.0f, 0.25f, -12.0f, 0.40f, 0.30f, "Rhythmic" },
        { "Ambient Drift Fills",        1.2f, 0.50f, -22.0f, 0.25f, 0.30f, "Ambient" },
        { "Liquid DnB Rollers",         8.5f, 0.20f, -12.0f, 0.45f, 0.25f, "Rhythmic" },
        { "House Groove Edits",         4.0f, 0.30f, -14.0f, 0.45f, 0.25f, "Subtle" },
        { "Hip-Hop Dust Chops",         3.0f, 0.38f, -16.0f, 0.45f, 0.20f, "Glitch" },
        { "Techno Rumble Gaps",         5.5f, 0.25f, -11.0f, 0.50f, 0.25f, "Destructive" },
        { "Dub Siren Throws",           2.0f, 0.45f, -19.0f, 0.35f, 0.20f, "Dub" },
        { "Dub Tape Spin Outs",         2.8f, 0.40f, -17.0f, 0.40f, 0.20f, "Dub" },
        { "Sidechain Kick Gaps",        3.5f, 0.35f, -14.0f, 0.55f, 0.15f, "Utility" },
        { "Hybrid Safe Bus",            3.0f, 0.33f, -15.0f, 0.40f, 0.25f, "Utility" },
        { "Whisper Reverses",           2.2f, 0.42f, -21.0f, 0.30f, 0.25f, "Subtle" },
        { "Soft Filter Breaths",        2.8f, 0.40f, -20.0f, 0.30f, 0.25f, "Subtle" },
        { "Start Here - Drum Bus",      5.0f, 0.30f, -15.0f, 0.40f, 0.25f, "Starting Points" },
        { "Start Here - Full Mix",      3.0f, 0.32f, -14.0f, 0.40f, 0.25f, "Starting Points" },
        { "Start Here - Vocals",        1.8f, 0.45f, -19.0f, 0.15f, 0.30f, "Starting Points" },
        { "Try One Algorithm - Reverse", 3.5f, 0.38f, -17.0f, 0.35f, 0.25f, "Starting Points" },
        { "Try One Algorithm - Stutter", 6.0f, 0.28f, -14.0f, 0.40f, 0.25f, "Starting Points" },
        { "Try Spectral Lanes - Highs", 4.5f, 0.30f, -16.0f, 0.20f, 0.45f, "Starting Points" },
        { "Stutter Gate Chops",         7.5f, 0.22f, -12.0f, 0.40f, 0.30f, "Glitch" },
        { "Half-Time Gap Rolls",        3.5f, 0.38f, -15.0f, 0.40f, 0.25f, "Rhythmic" },
        { "Crushed Gap Debris",         6.5f, 0.25f, -11.0f, 0.40f, 0.35f, "Destructive" },
        { "Shimmer Tail Wash",          1.0f, 0.55f, -23.0f, 0.25f, 0.35f, "Ambient" },
    };

    return table;
}

namespace
{
    struct Norm { float f[5]; };

    float clamp01 (float v) noexcept { return std::clamp (v, 0.0f, 1.0f); }

    Norm normalise (float eventsPerSec, float gap, float levelDb, float low, float high) noexcept
    {
        return { { clamp01 (std::log2 (1.0f + std::max (0.0f, eventsPerSec)) / std::log2 (17.0f)),
                   clamp01 (gap), clamp01 ((levelDb + 60.0f) / 60.0f), clamp01 (low), clamp01 (high) } };
    }

    constexpr float kWeights[5] = { 2.0f, 2.0f, 1.0f, 1.0f, 1.0f };

    int presetIndexByName (const char* name) noexcept
    {
        const auto& all = getFactoryPresets();

        for (int i = 0; i < (int) all.size(); ++i)
            if (std::strcmp (all[(size_t) i].name, name) == 0)
                return i;

        return -1;
    }
}

int rankPresets (const InputProfile& profile, Suggestion out[3])
{
    if (! profile.valid)
        return 0;

    const auto n = normalise (profile.eventsPerSec, profile.gapFraction, profile.levelDb, profile.lowShare, profile.highShare);

    struct Scored { int index; float distance; };
    std::vector<Scored> scored;

    for (const auto& row : familyTable())
    {
        const auto index = presetIndexByName (row.preset);

        if (index <= 0)                                       // unknown name, or "Init"
            continue;

        const auto c = normalise (row.eventsPerSec, row.gapFraction, row.levelDb, row.lowShare, row.highShare);
        float sum = 0.0f;

        for (int i = 0; i < 5; ++i)
            sum += kWeights[i] * (n.f[i] - c.f[i]) * (n.f[i] - c.f[i]);

        scored.push_back ({ index, std::sqrt (sum) });
    }

    std::stable_sort (scored.begin(), scored.end(), [] (const Scored& a, const Scored& b)
    {
        return a.distance != b.distance ? a.distance < b.distance : a.index < b.index;
    });

    // The input's most striking feature names the reason.
    int striking = 0;
    float strongest = -1.0f;

    for (int i = 0; i < 5; ++i)
    {
        const auto magnitude = kWeights[i] * std::abs (n.f[i] - 0.5f);

        if (magnitude > strongest)
        {
            strongest = magnitude;
            striking = i;
        }
    }

    const bool high = n.f[striking] >= 0.5f;
    const Reason reason = striking == 0 ? (high ? Reason::DenseHits : Reason::SparseHits)
                        : striking == 1 ? (high ? Reason::LongGaps : Reason::ShortGaps)
                        : striking == 2 ? (high ? Reason::Loud : Reason::Quiet)
                        : striking == 3 ? (high ? Reason::BassHeavy : Reason::TrebleHeavy)
                                        : (high ? Reason::TrebleHeavy : Reason::BassHeavy);

    const int count = (int) std::min<size_t> (3, scored.size());
    const float d1 = count > 0 ? scored[0].distance : 1.0f;

    for (int i = 0; i < count; ++i)
    {
        out[i].presetIndex = scored[(size_t) i].index;
        out[i].distance = scored[(size_t) i].distance;
        out[i].confidence = d1 < 0.20f ? Confidence::Good : d1 > 0.45f ? Confidence::Weak : Confidence::Fair;
        out[i].reason = reason;
    }

    return count;
}

juce::String confidenceWord (Confidence c)
{
    return c == Confidence::Good ? "Good match" : c == Confidence::Fair ? "Fair match" : "Weak match";
}

juce::String describeReason (const InputProfile& p, Reason r)
{
    switch (r)
    {
        case Reason::DenseHits:    return "dense hits, " + juce::String (p.eventsPerSec, 1) + " per second";
        case Reason::SparseHits:   return "sparse hits, " + juce::String (p.eventsPerSec, 1) + " per second";
        case Reason::ShortGaps:    return "short gaps, " + juce::String (juce::roundToInt (p.gapFraction * 100.0f)) + " % of the time";
        case Reason::LongGaps:     return "long gaps, " + juce::String (juce::roundToInt (p.gapFraction * 100.0f)) + " % of the time";
        case Reason::Loud:         return "loud material, " + juce::String (juce::roundToInt (p.levelDb)) + " dB";
        case Reason::Quiet:        return "quiet material, " + juce::String (juce::roundToInt (p.levelDb)) + " dB";
        case Reason::BassHeavy:    return "bass-heavy, " + juce::String (juce::roundToInt (p.lowShare * 100.0f)) + " % low band";
        case Reason::TrebleHeavy:  return "bright, " + juce::String (juce::roundToInt (p.highShare * 100.0f)) + " % high band";
    }

    return {};
}

juce::String measuredSummary (const InputProfile& p)
{
    return juce::String (p.eventsPerSec, 1) + " hits/s, gaps " + juce::String (juce::roundToInt (p.gapFraction * 100.0f))
         + " % of the time, " + juce::String (juce::roundToInt (p.levelDb)) + " dB";
}

std::vector<int> presetsWithoutRow()
{
    std::vector<int> missing;
    const auto& all = getFactoryPresets();

    for (int i = 1; i < (int) all.size(); ++i)                 // 0 is Init
    {
        bool found = false;

        for (const auto& row : familyTable())
            found = found || std::strcmp (row.preset, all[(size_t) i].name) == 0;

        if (! found)
            missing.push_back (i);
    }

    return missing;
}

} // namespace gapfill::suggest
