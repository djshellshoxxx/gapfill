#include "../support/TestRig.h"
#include "features/modelversions/State.h"

using namespace gapfill;
using namespace gftest;

GF_TEST (ModelVersions, old_parameter_snapshots_select_legacy_and_upgrade_is_undoable)
{
    GapFillAudioProcessor proc;
    GF_EXPECT (proc.getModelVersions() == models::ModelVersions {});
    auto old = proc.getValueTreeState().copyState();
    old.removeChild (old.getChildWithName (models::kTreeType), nullptr);
    GF_EXPECT (proc.applyParameterState (old));
    GF_EXPECT (proc.getModelVersions() == models::ModelVersions::legacy());
    proc.upgradeModels();
    GF_EXPECT (proc.getModelVersions() == models::ModelVersions {});
    GF_EXPECT (proc.undo());
    GF_EXPECT (proc.getModelVersions() == models::ModelVersions::legacy());
    proc.resetAllParameters();
    GF_EXPECT (proc.getModelVersions() == models::ModelVersions {});
    GF_EXPECT (proc.undo());
    GF_EXPECT (proc.getModelVersions() == models::ModelVersions::legacy());
    proc.selectPreset (0);
    GF_EXPECT (proc.getModelVersions() == models::ModelVersions {});
}

GF_TEST (ModelVersions, project_and_user_preset_roundtrips_keep_each_selected_model)
{
    GapFillAudioProcessor source;
    const models::ModelVersions selected { 2, 1, 2 };
    source.setModelVersions (selected, false);
    juce::MemoryBlock data;
    source.getStateInformation (data);
    GapFillAudioProcessor restored;
    restored.setStateInformation (data.getData(), (int) data.getSize());
    GF_EXPECT (restored.getModelVersions() == selected);

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("gapfill-models", ".gapfill");
    juce::String error;
    const bool saved = source.saveParameterStateToFile (file, error);
    const bool loaded = saved && restored.loadParameterStateFromFile (file, error);
    file.deleteFile();
    GF_EXPECT_MSG (saved && loaded, error);
    GF_EXPECT (restored.getModelVersions() == selected);
}
