#include "MidiOutState.h"
#include "MidiOut.h"
#include "../../PluginProcessor.h"

#include <cmath>

namespace gapfill::midiout
{

namespace
{
    const juce::Identifier perfType ("Perf");
    const juce::Identifier offsetProp ("midiOutOffsetMs");
}

bool validatePerf (const juce::ValueTree& tree)
{
    if (! tree.hasProperty (offsetProp))
        return true;                                    // absent attribute = default

    const auto v = tree.getProperty (offsetProp);

    // XML round trips turn numbers into strings; accept only plain decimal text.
    const bool numeric = v.isInt() || v.isInt64() || v.isDouble()
                          || (v.isString() && v.toString().isNotEmpty()
                              && v.toString().containsOnly ("0123456789.-+"));

    if (! numeric)
        return false;

    const auto ms = (double) v;
    return std::isfinite (ms) && std::abs (ms) <= (double) kMaxOffsetMs;
}

void applyPerf (GapFillAudioProcessor& proc, const juce::ValueTree& tree)
{
    proc.setMidiOutOffsetMs (tree.hasProperty (offsetProp) ? (double) tree.getProperty (offsetProp) : 0.0);
}

juce::ValueTree serialisePerf (const GapFillAudioProcessor& proc)
{
    const auto ms = proc.getMidiOutOffsetMs();

    if (std::abs (ms) < 1.0e-9)
        return {};                                      // nothing to remember

    juce::ValueTree tree (perfType);
    tree.setProperty (offsetProp, ms, nullptr);
    return tree;
}

void resetPerf (GapFillAudioProcessor& proc)
{
    proc.setMidiOutOffsetMs (0.0);
}

} // namespace gapfill::midiout
