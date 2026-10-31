#include "../support/TestRig.h"
#include "features/presetbrowser/PresetNavigation.h"
#include "features/presetbrowser/PresetFiles.h"
#include "features/presetbrowser/GlobalSettings.h"
#include "features/i18n/Strings.h"

using namespace gapfill;
using namespace gapfill::presets;
using namespace gftest;

namespace
{
    struct NavigationFiles
    {
        juce::File root = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("gapfill-nav-" + juce::Uuid().toString());
        std::shared_ptr<settings::GlobalSettings> preferences;

        NavigationFiles()
        {
            root.createDirectory();
            preferences = std::make_shared<settings::GlobalSettings> (root.getChildFile ("settings.xml"));
        }
        ~NavigationFiles() { preferences.reset(); root.deleteRecursively(); }
        std::vector<ScanRoot> roots() const { return { { "test", root } }; }
    };
}

GF_TEST (PresetNavigation, closed_editor_steps_factory_and_user_presets_with_one_undo_burst)
{
    NavigationFiles files;
    GapFillAudioProcessor proc;
    auto& navigation = proc.getUiState().presetNavigation;
    const auto user = files.root.getChildFile ("User.gapfill");
    setParams (proc, { { "fill", 73.0f } });
    juce::String error;
    GF_EXPECT (writeUserPreset (proc, user, { "User tone", "Ambient", "fixture", "" }, error));
    proc.selectPreset (proc.getNumPrograms() - 1);
    const auto beforeFill = proc.getValueTreeState().getRawParameterValue ("fill")->load();
    const auto depth = proc.getUndoDepth();
    GF_EXPECT (navigation.stepNowForTests (proc, 1, 1000, files.roots(), files.preferences));
    GF_EXPECT (navigation.currentFile() == user && navigation.displayName() == "User tone");
    GF_EXPECT (proc.getValueTreeState().getRawParameterValue ("fill")->load() == 73.0f);
    GF_EXPECT (proc.getUndoDepth() == depth + 1);
    GF_EXPECT (navigation.stepNowForTests (proc, 1, 2000, files.roots(), files.preferences));
    GF_EXPECT (proc.getCurrentProgram() == 0 && navigation.currentFile() == juce::File());
    GF_EXPECT (proc.getUndoDepth() == depth + 1);
    GF_EXPECT (proc.undo());
    GF_EXPECT (proc.getValueTreeState().getRawParameterValue ("fill")->load() == beforeFill);
    GF_EXPECT (proc.getActiveEditor() == nullptr);
}

GF_TEST (PresetNavigation, favourites_use_shared_user_identity_and_empty_favourites_fall_back)
{
    NavigationFiles files;
    GapFillAudioProcessor proc;
    auto& navigation = proc.getUiState().presetNavigation;
    const auto user = files.root.getChildFile ("Favourite.gapfill");
    juce::String error;
    GF_EXPECT (writeUserPreset (proc, user, { "Favourite user", "Ambient", "fixture", "" }, error));
    files.preferences->setInt (kKeyStepScope, 1);
    GF_EXPECT (navigation.stepNowForTests (proc, 1, 1000, files.roots(), files.preferences));
    GF_EXPECT (navigation.status() == i18n::text ("No favourites yet - stepping all presets"));
    juce::String identity;
    for (const auto& entry : navigation.catalog().entries())
        if (entry.file == user)
            identity = entry.identity;
    GF_EXPECT (identity.isNotEmpty() && files.preferences->setFavourite (identity, true));
    GF_EXPECT (navigation.stepNowForTests (proc, 1, 2000, files.roots(), files.preferences));
    GF_EXPECT (navigation.currentFile() == user && navigation.currentIndex (proc) >= proc.getNumPrograms());
    GF_EXPECT (navigation.stepNowForTests (proc, -1, 6000, files.roots(), files.preferences));
    GF_EXPECT (navigation.currentFile() == user);
    files.preferences->setFavourite (navigation.catalog().entries()[0].identity, true);
    files.preferences->setFavourite (navigation.catalog().entries()[1].identity, true);
    proc.selectPreset (0);
    GF_EXPECT (navigation.stepNowForTests (proc, 2, 7000, files.roots(), files.preferences));
    GF_EXPECT (navigation.currentFile() == user); // coalesced requests keep every step, including favourites
    navigation.requestReset();
    navigation.pump (proc);
    GF_EXPECT (navigation.currentFile() == juce::File());
}
