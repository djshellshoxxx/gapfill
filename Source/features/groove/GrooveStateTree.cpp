#include "GrooveStateTree.h"

#include "../../PluginProcessor.h"

#include <cmath>

namespace gapfill::groove
{

namespace
{
    constexpr const char* kType = "Groove";

    bool parseOffsets (const juce::String& text, float (&out)[kSlots])
    {
        juce::StringArray parts;
        parts.addTokens (text, ",", "");

        if (parts.size() != kSlots)
            return false;

        for (int i = 0; i < kSlots; ++i)
        {
            const auto p = parts[i].trim();

            if (p.isEmpty() || p.length() > 12 || ! p.containsOnly ("-+.0123456789eE"))
                return false;

            const auto v = p.getFloatValue();

            if (! std::isfinite (v) || std::abs (v) > kMaxOffsetSteps + 1.0e-4f)
                return false;

            out[i] = v;
        }

        return true;
    }
}

bool validateTree (const juce::ValueTree& t)
{
    if (! t.hasType (kType) || t.getNumChildren() != 0 || t.getNumProperties() > 6)
        return false;

    if (t.getProperty ("v", 0).toString().getIntValue() != 1)
        return false;

    float o[kSlots];

    if (! parseOffsets (t.getProperty ("o").toString(), o))
        return false;

    const auto conf = t.getProperty ("conf", 0.0).toString();

    if (conf.isEmpty() || ! conf.containsOnly ("0123456789."))
        return false;

    const auto c = conf.getFloatValue();
    return std::isfinite (c) && c >= 0.0f && c <= 1.0f;
}

void applyTree (GapFillAudioProcessor& p, const juce::ValueTree& t)
{
    GrooveSnapshot snap;
    float o[kSlots];

    if (! parseOffsets (t.getProperty ("o").toString(), o))
        return;

    for (int i = 0; i < kSlots; ++i)
        snap.offsetSteps[i] = o[i];

    snap.confidence = t.getProperty ("conf").toString().getFloatValue();
    p.getContextState().grooveState().requestLoad (snap, t.getProperty ("hold", 0).toString().getIntValue() == 0);
}

juce::ValueTree serialiseTree (const GapFillAudioProcessor& p)
{
    const auto& gs = p.getContextState().grooveState();
    const bool holdParam = p.getValueTreeState().getRawParameterValue ("fl09_hold")->load() > 0.5f;

    // A restore queues its request for the audio thread. Until the next chunk runs it, describe the queued state
    // (a pending reset means "no groove", a pending load is the groove to write back), so that set-state followed
    // directly by get-state is reproducible.
    GrooveSnapshot snap;
    bool latchedByLearn = gs.isFrozenByLearn();
    const bool pendingLoad = gs.peekPendingLoad (snap, latchedByLearn);

    if (! pendingLoad && gs.isResetPending())
        return {};

    if (! holdParam && ! latchedByLearn)
        return {};

    if (! pendingLoad && (! gs.readSnapshot (snap) || ! snap.valid))
        return {};

    juce::String offsets;

    for (int i = 0; i < kSlots; ++i)
        offsets << (i > 0 ? "," : "") << juce::String (snap.offsetSteps[i], 3);

    juce::ValueTree t (kType);
    t.setProperty ("v", 1, nullptr);
    t.setProperty ("hold", holdParam ? 1 : 0, nullptr);
    t.setProperty ("conf", juce::String (juce::jlimit (0.0f, 1.0f, snap.confidence), 2), nullptr);
    t.setProperty ("o", offsets, nullptr);
    return t;
}

void resetTree (GapFillAudioProcessor& p)
{
    p.getContextState().grooveState().requestReset();
}

} // namespace gapfill::groove
