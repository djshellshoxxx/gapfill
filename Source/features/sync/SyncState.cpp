#include "SyncState.h"
#include "../../PluginProcessor.h"

#include <cmath>

namespace gapfill::sync
{

namespace
{
    const juce::Identifier perfType ("Perf");
    const juce::Identifier fallbackBpmProp ("fallbackBpm");
}

bool validatePerf (const juce::ValueTree& tree)
{
    if (! tree.hasProperty (fallbackBpmProp))
        return true;                                    // absent attribute = default

    const auto v = tree.getProperty (fallbackBpmProp);

    // XML round trips turn numbers into strings; accept only plain decimal text.
    const bool numeric = v.isInt() || v.isInt64() || v.isDouble()
                          || (v.isString() && v.toString().isNotEmpty()
                              && v.toString().containsOnly ("0123456789.-+"));

    if (! numeric)
        return false;

    const auto bpm = (double) v;
    return std::isfinite (bpm) && bpm >= kMinFallbackBpm && bpm <= kMaxFallbackBpm;
}

void applyPerf (GapFillAudioProcessor& proc, const juce::ValueTree& tree)
{
    proc.getShaping().tempo.setFallbackBpm (tree.hasProperty (fallbackBpmProp)
                                                ? (double) tree.getProperty (fallbackBpmProp)
                                                : kDefaultFallbackBpm);
}

juce::ValueTree serialisePerf (const GapFillAudioProcessor& proc)
{
    const auto bpm = proc.getShaping().tempo.getFallbackBpm();

    if (std::abs (bpm - kDefaultFallbackBpm) < 1.0e-9)
        return {};                                      // nothing to remember

    juce::ValueTree tree (perfType);
    tree.setProperty (fallbackBpmProp, bpm, nullptr);
    return tree;
}

void resetPerf (GapFillAudioProcessor& proc)
{
    proc.getShaping().tempo.setFallbackBpm (kDefaultFallbackBpm);
}

} // namespace gapfill::sync
