#include "../../PluginProcessor.h"

namespace gapfill
{
coop::DeterministicConfig GapFillAudioProcessor::gatherCoopConfig() const noexcept
{
    coop::DeterministicConfig config;
    if (! pb (params.coopDeterministic))
        return config;
    const auto group = pi (params.coopDetGroup);
    config.group = group != 0 ? group : pi (params.coopGroup);
    config.slot = pi (params.coopSlot);
    config.priority = pi (params.coopPriority);
    config.yieldPercent = pv (params.coopYield);
    config.grid = (coop::deterministic::Grid) juce::jlimit (0, 4, pi (params.coopGrid));
    return config;
}

void GapFillAudioProcessor::pumpCoop()
{
    coopMember.repairDuplicateSlot();
    if (auto* slot = apvts.getParameter ("coopSlot"))
    {
        const auto pending = coopMember.pendingSlotForHost (pi (params.coopSlot));
        if (pending >= 1 && pending <= 32 && pending != pi (params.coopSlot))
        {
            slot->beginChangeGesture();
            slot->setValueNotifyingHost (slot->convertTo0to1 ((float) pending));
            slot->endChangeGesture();
        }
    }
}
} // namespace gapfill
