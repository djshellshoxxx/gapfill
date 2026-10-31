#include "../../PluginProcessor.h"
#include "State.h"

namespace gapfill
{
models::ModelVersions GapFillAudioProcessor::getModelVersions() const noexcept
{
    return models::ModelVersions::fromPacked (modelVersionsPacked.load (std::memory_order_acquire));
}

void GapFillAudioProcessor::setModelVersions (models::ModelVersions versions, bool notifyHost)
{
    if (! versions.valid())
        versions = models::ModelVersions::legacy();
    for (int i = apvts.state.getNumChildren(); --i >= 0;)
        if (apvts.state.getChild (i).hasType (models::kTreeType))
            apvts.state.removeChild (i, nullptr);
    apvts.state.appendChild (models::toModelVersionsTree (versions), nullptr);
    modelVersionsPacked.store (versions.packed(), std::memory_order_release);
    if (notifyHost)
    {
        currentProgram.store (-1, std::memory_order_relaxed);
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    }
}

void GapFillAudioProcessor::syncModelVersions()
{
    int count = 0;
    for (int i = 0; i < apvts.state.getNumChildren(); ++i)
        count += apvts.state.getChild (i).hasType (models::kTreeType) ? 1 : 0;
    const auto versions = count == 1
        ? models::fromModelVersionsTree (apvts.state.getChildWithName (models::kTreeType))
        : models::ModelVersions::legacy();
    modelVersionsPacked.store (versions.packed(), std::memory_order_release);
}

void GapFillAudioProcessor::upgradeModels()
{
    if (getModelVersions() == models::ModelVersions {})
        return;
    pushUndoSnapshot();
    setModelVersions ({}, true);
}
} // namespace gapfill
