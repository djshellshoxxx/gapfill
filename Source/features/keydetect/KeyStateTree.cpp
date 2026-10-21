#include "KeyStateTree.h"

#include "../../PluginProcessor.h"

namespace gapfill::keydetect
{

namespace
{
    constexpr const char* kType = "Key";

    bool intIn (const juce::ValueTree& t, const char* name, int lo, int hi)
    {
        if (! t.hasProperty (name))
            return false;

        const auto text = t.getProperty (name).toString().trim();

        if (text.isEmpty() || text.length() > 4 || ! text.containsOnly ("-0123456789"))
            return false;

        const auto n = text.getIntValue();
        return n >= lo && n <= hi;
    }
}

bool validateKeyTree (const juce::ValueTree& t)
{
    return t.hasType (kType) && t.getNumChildren() == 0 && t.getNumProperties() <= 5
        && intIn (t, "tonic", 0, 11) && intIn (t, "mode", 0, 1) && intIn (t, "conf", 0, 100)
        && (! t.hasProperty ("hold") || intIn (t, "hold", 0, 1));
}

void applyKeyTree (GapFillAudioProcessor& p, const juce::ValueTree& t)
{
    KeyEstimate e;
    e.tonic = (std::int8_t) t.getProperty ("tonic").toString().getIntValue();
    e.mode = (std::uint8_t) t.getProperty ("mode").toString().getIntValue();
    e.confidence = (std::uint8_t) t.getProperty ("conf").toString().getIntValue();
    e.valid = true;
    p.getContextState().autoKey().requestWarmStart (e);
}

juce::ValueTree serialiseKeyTree (const GapFillAudioProcessor& p)
{
    const auto& vts = p.getValueTreeState();
    const bool autoOn = vts.getRawParameterValue ("scaleFollow")->load() > 0.5f;
    const bool hold = vts.getRawParameterValue ("fx03_hold")->load() > 0.5f;

    if (! autoOn && ! hold)
        return {};

    const auto& ak = p.getContextState().autoKey();
    auto e = hold && ak.heldKey().valid ? ak.heldKey() : ak.estimate();

    if (! e.valid || e.tonic < 0)
        return {};

    juce::ValueTree t (kType);
    t.setProperty ("tonic", (int) e.tonic, nullptr);
    t.setProperty ("mode", (int) e.mode, nullptr);
    t.setProperty ("conf", (int) e.confidence, nullptr);
    t.setProperty ("hold", hold ? 1 : 0, nullptr);
    return t;
}

void resetKeyTree (GapFillAudioProcessor& p)
{
    p.getContextState().autoKey().requestReset();
}

} // namespace gapfill::keydetect
