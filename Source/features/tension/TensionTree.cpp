// F-M21 <Tension version="1"> state subtree (<= 2 KiB): four <Lane i interp pts="x:y,..."/> children.
// Validation is atomic: any violation drops the subtree to the flat defaults and the rest of the state loads.

#include "TensionTree.h"

#include "Tension.h"
#include "../../PluginProcessor.h"

namespace gapfill::tension
{

namespace
{
    constexpr const char* kType = "Tension";

    bool parseLane (const juce::ValueTree& t, TensionLane& out)
    {
        const auto interp = t.getProperty ("interp").toString();

        if (interp.isEmpty() || ! interp.containsOnly ("012") || interp.length() != 1)
            return false;

        juce::StringArray pts;
        pts.addTokens (t.getProperty ("pts").toString(), ",", "");

        if (pts.size() < 2 || pts.size() > kMaxPoints)
            return false;

        TensionLane l;
        l.numPoints = (std::uint8_t) pts.size();
        l.interp = (std::uint8_t) interp.getIntValue();

        for (int i = 0; i < pts.size(); ++i)
        {
            const auto parts = juce::StringArray::fromTokens (pts[i], ":", "");

            if (parts.size() != 2 || ! parts[0].containsOnly ("0123456789") || ! parts[1].containsOnly ("0123456789")
                 || parts[0].isEmpty() || parts[1].isEmpty() || parts[0].length() > 4 || parts[1].length() > 4)
                return false;

            const int x = parts[0].getIntValue(), y = parts[1].getIntValue();

            if (x > 1000 || y > 1000)
                return false;

            l.pt[i].x = (std::uint16_t) x;
            l.pt[i].y = (std::uint16_t) y;
        }

        out = l;
        return true;
    }

    bool parseAll (const juce::ValueTree& t, TensionCurves& out)
    {
        bool seen[kLanes] {};

        for (int c = 0; c < t.getNumChildren(); ++c)
        {
            const auto lane = t.getChild (c);
            const auto idx = lane.getProperty ("i").toString();

            if (! lane.hasType ("Lane") || idx.length() != 1 || ! idx.containsOnly ("0123"))
                return false;

            const int i = idx.getIntValue();

            if (seen[i] || ! parseLane (lane, out.lane[i]))
                return false;

            seen[i] = true;
        }

        for (bool s : seen)
            if (! s)
                return false;

        return validate (out);
    }
}

bool validateTree (const juce::ValueTree& t)
{
    TensionCurves c;
    return t.hasType (kType) && t.getNumChildren() == kLanes && parseAll (t, c);
}

void applyTree (GapFillAudioProcessor& p, const juce::ValueTree& t)
{
    TensionCurves c;

    if (parseAll (t, c))
        p.getContextState().tensionService().publishCurves (c);
}

juce::ValueTree serialiseTree (const GapFillAudioProcessor& p)
{
    const auto& svc = p.getContextState().tensionService();

    if (svc.isDefault())
        return {};                                          // flat curves: write nothing

    const auto c = svc.curves();
    juce::ValueTree t (kType);
    t.setProperty ("version", 1, nullptr);

    for (int i = 0; i < kLanes; ++i)
    {
        juce::String pts;

        for (int k = 0; k < c.lane[i].numPoints; ++k)
            pts << (k > 0 ? "," : "") << (int) c.lane[i].pt[k].x << ":" << (int) c.lane[i].pt[k].y;

        juce::ValueTree lane ("Lane");
        lane.setProperty ("i", i, nullptr);
        lane.setProperty ("interp", (int) c.lane[i].interp, nullptr);
        lane.setProperty ("pts", pts, nullptr);
        t.appendChild (lane, nullptr);
    }

    return t;
}

void resetTree (GapFillAudioProcessor& p)
{
    p.getContextState().tensionService().publishCurves (TensionCurves {});
}

} // namespace gapfill::tension
