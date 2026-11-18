#include "../support/TestRig.h"
#include "../support/LiveRig.h"

using namespace gapfill;
using namespace gftest;

GF_TEST (CoopProcessor, duplicate_slots_are_saved_and_repaired_without_an_open_editor)
{
    auto first = std::make_unique<GapFillAudioProcessor>();
    auto second = std::make_unique<GapFillAudioProcessor>();
    for (auto* proc : { first.get(), second.get() })
    {
        setParams (*proc, { { "coopDeterministic", 1 }, { "coopDetGroup", 8 }, { "coopSlot", 1 } });
        prepareProcessor (*proc, 48000.0, 64, false);
    }
    GF_EXPECT (first->getCoop().assignedSlot() == 1);
    GF_EXPECT (second->getCoop().assignedSlot() == 2);
    GF_EXPECT (second->getValueTreeState().getRawParameterValue ("coopSlot")->load() == 1.0f);

    juce::MemoryBlock data;
    second->getStateInformation (data);
    GapFillAudioProcessor restored;
    restored.setStateInformation (data.getData(), (int) data.getSize());
    GF_EXPECT (restored.getValueTreeState().getRawParameterValue ("coopSlot")->load() == 2.0f);
    second->flushLatencyReportForTests();
    GF_EXPECT (second->getValueTreeState().getRawParameterValue ("coopSlot")->load() == 2.0f);
    first->releaseResources();
    second->releaseResources();
    GF_EXPECT (first->getCoop().getGroup() == 0 && second->getCoop().getGroup() == 0);
}
