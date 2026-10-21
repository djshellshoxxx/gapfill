#include "TasteStateTree.h"

#include "../../PluginProcessor.h"

namespace gapfill::taste
{

namespace
{
    constexpr const char* kType = "Taste";

    bool parseQ (const juce::String& text, std::int8_t (&q)[kFeatures])
    {
        juce::StringArray parts;
        parts.addTokens (text, ",", "");

        if (parts.size() != kFeatures)
            return false;

        for (int i = 0; i < kFeatures; ++i)
        {
            const auto p = parts[i].trim();

            if (p.isEmpty() || p.length() > 4 || ! p.containsOnly ("-0123456789"))
                return false;

            const int v = p.getIntValue();

            if (v < -44 || v > 44)
                return false;

            q[i] = (std::int8_t) v;
        }

        return true;
    }

    bool intIn (const juce::ValueTree& t, const char* name, int lo, int hi)
    {
        const auto text = t.getProperty (name).toString().trim();
        return ! text.isEmpty() && text.length() <= 6 && text.containsOnly ("0123456789")
            && text.getIntValue() >= lo && text.getIntValue() <= hi;
    }
}

bool validateTasteTree (const juce::ValueTree& t)
{
    std::int8_t q[kFeatures];
    return t.hasType (kType) && t.getNumChildren() == 0 && t.getNumProperties() <= 6
        && t.getProperty ("v").toString().getIntValue() == 1
        && intIn (t, "likes", 0, 65535) && intIn (t, "skips", 0, 65535) && intIn (t, "ready", 0, 1)
        && parseQ (t.getProperty ("q").toString(), q);
}

void applyTasteTree (GapFillAudioProcessor& p, const juce::ValueTree& t)
{
    ProjectProfile pr;

    if (! parseQ (t.getProperty ("q").toString(), pr.q))
        return;

    pr.likes = (std::uint16_t) t.getProperty ("likes").toString().getIntValue();
    pr.skips = (std::uint16_t) t.getProperty ("skips").toString().getIntValue();
    pr.ready = t.getProperty ("ready").toString().getIntValue() != 0;
    p.getContextState().tasteService().setProjectProfile (pr);
}

juce::ValueTree serialiseTasteTree (const GapFillAudioProcessor& p)
{
    const auto pr = p.getContextState().tasteService().projectProfile();

    if (! pr.ready && pr.likes == 0 && pr.skips == 0)
        return {};                                    // nothing learned: write nothing

    juce::String q;

    for (int i = 0; i < kFeatures; ++i)
        q << (i > 0 ? "," : "") << (int) pr.q[i];

    juce::ValueTree t (kType);
    t.setProperty ("v", 1, nullptr);
    t.setProperty ("likes", (int) pr.likes, nullptr);
    t.setProperty ("skips", (int) pr.skips, nullptr);
    t.setProperty ("ready", pr.ready ? 1 : 0, nullptr);
    t.setProperty ("q", q, nullptr);
    return t;
}

void resetTasteTree (GapFillAudioProcessor& p)
{
    p.getContextState().tasteService().resetProfile (false);
}

} // namespace gapfill::taste
