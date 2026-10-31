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
    models::writeToParameterState (apvts.state, versions);
    modelVersionsPacked.store (versions.packed(), std::memory_order_release);
    contextState.tasteService().setModelVersion (versions.taste);
    if (notifyHost)
    {
        currentProgram.store (-1, std::memory_order_relaxed);
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    }
}

void GapFillAudioProcessor::syncModelVersions()
{
    const auto versions = models::fromParameterState (apvts.state);
    modelVersionsPacked.store (versions.packed(), std::memory_order_release);
    contextState.tasteService().setModelVersion (versions.taste);
}

void GapFillAudioProcessor::upgradeModels()
{
    if (getModelVersions() == models::ModelVersions {})
        return;
    pushUndoSnapshot();
    setModelVersions ({}, true);
}
} // namespace gapfill
