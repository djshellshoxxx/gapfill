// U-8 / F-S03 UI scale.

#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "features/presetbrowser/GlobalSettings.h"
#include "features/uiscale/UiScale.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    struct ScopedSettings
    {
        ScopedSettings()
        {
            file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("gapfill-scale-test-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".xml");
            settings::GlobalSettings::useFileForTests (file);
        }

        ~ScopedSettings()
        {
            settings::GlobalSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                         .getChildFile ("gapfill-scale-test-reset.xml"));
            file.deleteFile();
        }

        juce::File file;
    };
}

GF_TEST (UiScale, scale_steps_snap)
{
    using namespace uiscale;
    GF_EXPECT (snapScalePercent (100) == 100 && snapScalePercent (125) == 125 && snapScalePercent (200) == 200);
    GF_EXPECT (snapScalePercent (130) == 125 && snapScalePercent (140) == 150 && snapScalePercent (199) == 200);
    GF_EXPECT (snapScalePercent (0) == 100 && snapScalePercent (-5) == 100 && snapScalePercent (9999) == 100);
}

GF_TEST (UiScale, scale_clamps_to_display)
{
    using namespace uiscale;
    const juce::Point<int> logical { 1100, 700 };

    GF_EXPECT (clampScalePercent (200, { 0, 0, 3840, 2160 }, logical, 1.0f) == 200);
    GF_EXPECT (clampScalePercent (200, { 0, 0, 1920, 1080 }, logical, 1.0f) == 125);   // 700 * 1.5 = 1050 > 1026 (95 % of 1080), so 125
    GF_EXPECT (clampScalePercent (200, { 0, 0, 1920, 1080 }, { 1000, 600 }, 1.0f) == 150);   // 600 * 1.75 = 1050 > 1026, 600 * 1.5 = 900 fits
    GF_EXPECT (clampScalePercent (150, { 0, 0, 1920, 1080 }, { 1000, 600 }, 1.0f) == 150);
    GF_EXPECT (clampScalePercent (100, { 0, 0, 1920, 1080 }, logical, 1.0f) == 100);
    GF_EXPECT (clampScalePercent (150, { 0, 0, 1920, 1080 }, logical, 2.0f) == 100);   // host scale 2 already uses the screen
    GF_EXPECT_MSG (clampScalePercent (200, { 0, 0, 800, 600 }, logical, 1.0f) == 100, "never below 100 even if the screen is smaller than the editor");
    GF_EXPECT (clampScalePercent (175, {}, logical, 1.0f) == 175);                    // unknown work area: trust the setting
}

GF_TEST (UiScale, effective_scale_composes_host_and_user)
{
    using namespace uiscale;
    GF_EXPECT_NEAR (effectiveScale (1.0f, 100), 1.0, 1e-6);
    GF_EXPECT_NEAR (effectiveScale (2.0f, 150), 3.0, 1e-6);
    GF_EXPECT_NEAR (effectiveScale (1.25f, 125), 1.5625, 1e-6);
    GF_EXPECT_NEAR (effectiveScale (0.0f, 200), 2.0, 1e-6);
}

GF_TEST (UiScale, scale_applied_and_persisted)
{
    ScopedSettings scoped;
    auto settingsFile = settings::GlobalSettings::shared();
    settingsFile->setInt (settings::GlobalSettings::keyUiScale, 150);

    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1100, 700);
    editor.applyScaleForTests();

    const auto applied = editor.getAppliedScalePercentForTests();
    GF_EXPECT_MSG (applied == 150 || applied == 125 || applied == 100, "clamped to the display, never above the request");

    if (applied == 150)
        GF_EXPECT_NEAR (editor.getTransform().getScaleFactor(), 1.5, 1e-4);

    // Survives a restart: written file is re-read by a new settings object.
    settingsFile->flush();
    settings::GlobalSettings reread (scoped.file);
    GF_EXPECT (reread.getInt (settings::GlobalSettings::keyUiScale, 100) == 150);

    // Back to 100: no transform at all.
    settingsFile->setInt (settings::GlobalSettings::keyUiScale, 100);
    editor.applyScaleForTests();
    GF_EXPECT (editor.getAppliedScalePercentForTests() == 100);
    GF_EXPECT (editor.getTransform().isIdentity());
}

GF_TEST (UiScale, logical_size_unchanged_by_scale)
{
    ScopedSettings scoped;
    settings::GlobalSettings::shared()->setInt (settings::GlobalSettings::keyUiScale, 125);

    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1100, 700);
    editor.applyScaleForTests();

    GF_EXPECT (editor.getWidth() == 1100 && editor.getHeight() == 700);
    GF_EXPECT (proc.getSavedEditorSize().x == 1100 && proc.getSavedEditorSize().y == 700);
}

GF_TEST (UiScale, set_scale_factor_idempotent)
{
    ScopedSettings scoped;
    settings::GlobalSettings::shared()->setInt (settings::GlobalSettings::keyUiScale, 100);

    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1100, 700);

    editor.setScaleFactor (1.5f);
    const auto first = editor.getTransform();
    editor.setScaleFactor (1.5f);
    editor.setScaleFactor (1.5f);
    GF_EXPECT (editor.getTransform() == first);
    GF_EXPECT_NEAR (editor.getTransform().getScaleFactor(), 1.5, 1e-4);

    editor.setScaleFactor (1.0f);
    GF_EXPECT (editor.getTransform().isIdentity());
}

GF_TEST (UiScale, options_extra_area_is_inside_the_options_panel_at_min_size)
{
    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1020, 620);

    const auto area = editor.getOptionsExtraBoundsForTests();
    GF_EXPECT_MSG (area.getHeight() >= 100, "room for the interface size and accessibility rows");
    GF_EXPECT (editor.getOptionsBoundsForTests().withZeroOrigin().contains (area));
    GF_EXPECT (editor.getLocalBounds().contains (editor.getOptionsBoundsForTests()));
}
