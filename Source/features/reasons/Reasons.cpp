#include "Reasons.h"

namespace gapfill::reasons
{

namespace
{
    using R = RejectReason;

    // Order == enum order (checked below). Wording: docs/dev/03 E-3 catalogue and
    // docs/dev/12 section 2.2 for the reasons added by later tickets.
    constexpr std::array<ReasonInfo, (size_t) numRejectReasons> kTable =
    {{
        { R::None,              "Fill started",              "",                                                                          '-' },
        { R::FillOff,           "Fill is at 0",              "Raise Fill",                                                                'F' },
        { R::NoSource,          "No audio to sample",        "Play audio before the gap; check input level / Detector source",             'S' },
        { R::TooSoon,           "Too soon after last fill",  "Lower Retrig Guard / Min Gap",                                              'T' },
        { R::NoVoice,           "All fill voices busy",      "Lower Complexity or Max Fill",                                              'V' },
        { R::TooShort,          "Gap too short",             "Lower Min Gap, or set Lookahead to 0 / reduce Protect Next",                'G' },
        { R::GuardEaten,        "Next-event guard used the space", "Lower Protect Next",                                                  'E' },
        { R::NoBand,            "No spectral band qualified", "Enable a band or raise band probability",                                  'B' },
        { R::DensityRoll,       "Skipped by Density / Fill probability", "Raise Density or Fill",                                         'D' },
        { R::NoAlgoFits,        "No algorithm fits this length", "Raise Max Fill, enable shorter algorithms, or choose Fill Type = Auto", 'A' },
        { R::SourceUnavailable, "Fill couldn't be built",    "Internal - report it if this happens often",                                'X' },
        { R::LockSkipped,       "Locked fill doesn't fit this gap", "Unlock, or lengthen gaps (see Fill Lock)",                           'L' },
        { R::BudgetFull,        "Fill budget for this bar used", "Raise Fills per bar",                                                  'N' },
        { R::QuietZone,         "Inside a quiet zone",       "Change Quiet Zone",                                                         'Q' },
        { R::ScGated,           "Sidechain gate closed",     "Lower SC threshold or change gate mode",                                    'C' },
        { R::Panicked,          "Panic is active",           "Press Recover",                                                             'P' },
        { R::CoopYield,         "Yielded to another GapFill", "Change Co-op mode",                                                        'Y' },
        { R::ForcedUnsafe,      "Fill Now skipped - next hit too close", "Fire earlier or lower Protect Next",                            'Z' },
    }};

    constexpr ReasonInfo kUnknown { R::None, "Unknown reason", "", '?' };

    constexpr bool tableIsInEnumOrder()
    {
        for (size_t i = 0; i < kTable.size(); ++i)
            if ((size_t) kTable[i].reason != i)
                return false;

        return true;
    }

    static_assert (tableIsInEnumOrder(), "reason table must follow the RejectReason enum");
}

const ReasonInfo& info (RejectReason r) noexcept
{
    const auto i = (size_t) r;
    return i < kTable.size() ? kTable[i] : kUnknown;
}

bool showsNumbers (RejectReason r) noexcept
{
    return r == R::TooShort || r == R::GuardEaten || r == R::NoAlgoFits;
}

juce::String labelWithNumbers (RejectReason r, float availMs, float needMs)
{
    juce::String s (info (r).label);

    if (showsNumbers (r) && needMs > 0.0f)
        s << " (" << juce::String ((int) std::lround (juce::jmax (0.0f, availMs))) << " ms free, need "
          << juce::String ((int) std::lround (needMs)) << " ms)";

    return s;
}

juce::String hoverText (RejectReason r, float availMs, float needMs)
{
    juce::String s (info (r).label);

    if (showsNumbers (r) && needMs > 0.0f)
        s << " - " << juce::String ((int) std::lround (juce::jmax (0.0f, availMs))) << " ms free, "
          << juce::String ((int) std::lround (needMs)) << " ms needed";

    const juce::String a (info (r).advice);

    if (a.isNotEmpty())
        s << ". " << a;

    return s;
}

juce::String statusLine (const FillEvent& e)
{
    if (e.type == FillEventType::Started)
        return "Last gap: filled";

    if (e.type == FillEventType::Rejected)
        return "Last gap: skipped - " + labelWithNumbers (e.reason, e.availMs, e.needMs);

    return {};
}

int topReasons (const std::uint32_t* counts, int numCounts, TopReason* out, int maxOut) noexcept
{
    if (counts == nullptr || out == nullptr || maxOut <= 0)
        return 0;

    int written = 0;

    for (int rank = 0; rank < maxOut; ++rank)
    {
        int best = -1;

        for (int i = 1; i < numCounts; ++i)       // skip None
        {
            if (counts[i] == 0)
                continue;

            bool taken = false;

            for (int w = 0; w < written; ++w)
                taken = taken || (int) out[w].reason == i;

            if (! taken && (best < 0 || counts[i] > counts[best]))
                best = i;
        }

        if (best < 0)
            break;

        out[written++] = { (RejectReason) best, counts[best] };
    }

    return written;
}

juce::String whySoFewFillsText (const std::uint32_t* counts, int numCounts)
{
    TopReason top[3];
    const auto n = topReasons (counts, numCounts, top, 3);

    if (n == 0)
        return "No gaps have been skipped in this session.";

    juce::String s ("Most common reasons gaps were skipped this session:\n");

    for (int i = 0; i < n; ++i)
        s << "\n" << juce::String (i + 1) << ". " << info (top[i].reason).label << " (" << juce::String ((int) top[i].count)
          << " gaps) - " << info (top[i].reason).advice;

    return s;
}

} // namespace gapfill::reasons
