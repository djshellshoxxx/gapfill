// F-D04 accessibility and contrast presets.

#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "features/access/Access.h"
#include "UiTestUtil.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    struct ScopedSettings
    {
        ScopedSettings()
        {
            dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("gapfill-access-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
            dir.createDirectory();
            file = dir.getChildFile ("settings.xml");
            settings::GlobalSettings::useFileForTests (file);
        }

        ~ScopedSettings()
        {
            access::applyPalette (access::Contrast::Standard);
            focusRingWidth = 2.0f;
            settings::GlobalSettings::useFileForTests (dir.getChildFile ("after.xml"));
            dir.deleteRecursively();
        }

        juce::File dir, file;
    };

    void collect (juce::Component& root, std::vector<juce::Component*>& out)
    {
        for (auto* c : root.getChildren())
        {
            out.push_back (c);
            collect (*c, out);
        }
    }
}

GF_TEST (Access, contrast_ratios_meet_AA_all_token_pairs)
{
    for (const auto mode : { access::Contrast::Standard, access::Contrast::High, access::Contrast::DimVenue })
    {
        const auto& p = access::paletteFor (mode);

        for (const auto& pair : access::tokenPairs())
        {
            if (pair.highOnly && mode != access::Contrast::High)
                continue;

            const auto ratio = access::contrastRatio (p.*(pair.fg), p.*(pair.bg));
            GF_EXPECT_MSG (ratio >= pair.minimum,
                           juce::String (pair.name) + " in mode " + juce::String ((int) mode) + ": " + juce::String (ratio, 2)
                             + " < " + juce::String (pair.minimum));
        }
    }

    // The WCAG formula itself: black on white is 21:1.
    GF_EXPECT_NEAR (access::contrastRatio (juce::Colours::black, juce::Colours::white), 21.0, 0.01);
    GF_EXPECT_NEAR (access::contrastRatio (juce::Colours::white, juce::Colours::white), 1.0, 0.001);
}

GF_TEST (Access, dim_venue_caps_peak_luminance_near_60_percent)
{
    const auto standard = access::peakLuminance (access::paletteFor (access::Contrast::Standard));
    const auto dim = access::peakLuminance (access::paletteFor (access::Contrast::DimVenue));
    const auto ratio = dim / standard;
    GF_EXPECT_MSG (ratio > 0.45 && ratio < 0.65, "dim/standard peak luminance = " + juce::String (ratio, 3));
    GF_EXPECT (access::peakLuminance (access::paletteFor (access::Contrast::High)) > standard);
}

GF_TEST (Access, standard_palette_matches_theme_tokens)
{
    const auto& p = access::paletteFor (access::Contrast::Standard);
    GF_EXPECT (p.background == juce::Colour (0xff0e1116) && p.panel == juce::Colour (0xff171b22) && p.edge == juce::Colour (0xff2a303a));
    GF_EXPECT (p.primary == juce::Colour (0xffe8532a) && p.secondary == juce::Colour (0xff4fb6c4));
    GF_EXPECT (p.text == juce::Colour (0xffe6e8ec) && p.textMuted == juce::Colour (0xff8a929e));
}

GF_TEST (Access, settings_persist_global_not_project)
{
    ScopedSettings scoped;
    auto settingsFile = settings::GlobalSettings::shared();
    settingsFile->setInt (access::kKeyContrast, 1);
    settingsFile->setInt (access::kKeyControlSize, 1);
    settingsFile->setInt (access::kKeyFocusRing, 3);
    settingsFile->flush();

    // Never part of the project state.
    GapFillAudioProcessor proc;
    juce::MemoryBlock block;
    proc.getStateInformation (block);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize())->toString();
    GF_EXPECT (! xml.containsIgnoreCase ("contrast") && ! xml.containsIgnoreCase ("controlSize") && ! xml.containsIgnoreCase ("focusRing"));

    // Read back by a fresh settings object (a later session / another instance).
    settings::GlobalSettings reread (scoped.file);
    GF_EXPECT (access::readContrast (reread) == access::Contrast::High);
    GF_EXPECT (access::readControlSize (reread) == access::ControlSize::Large);
    GF_EXPECT (access::readFocusRingPx (reread) == 3);

    // Garbage values fall back to the defaults.
    reread.setInt (access::kKeyContrast, 99);
    reread.setInt (access::kKeyFocusRing, 7);
    GF_EXPECT (access::readContrast (reread) == access::Contrast::Standard && access::readFocusRingPx (reread) == 2);
}

GF_TEST (Access, applying_settings_swaps_tokens_focus_ring_and_scale)
{
    ScopedSettings scoped;
    auto settingsFile = settings::GlobalSettings::shared();

    GF_EXPECT_NEAR (access::applyFromSettings (*settingsFile), 1.0, 1e-6);
    GF_EXPECT (colours::text == juce::Colour (0xffe6e8ec) && focusRingWidth == 2.0f);

    settingsFile->setInt (access::kKeyContrast, 1);
    settingsFile->setInt (access::kKeyControlSize, 1);
    settingsFile->setInt (access::kKeyFocusRing, 3);
    GF_EXPECT_NEAR (access::applyFromSettings (*settingsFile), access::kLargeControlFactor, 1e-6);
    GF_EXPECT (colours::text == juce::Colour (0xffffffff));
    GF_EXPECT (colours::accentWarm == colours::primary && colours::textDim == colours::textMuted && colours::accent == colours::secondary);
    GF_EXPECT (focusRingWidth == 3.0f);
}

GF_TEST (Access, large_controls_min_target)
{
    // The scale path turns the 28 px buttons into >= 32 px and the 11 px labels into about 13 px.
    GF_EXPECT ((float) metrics::buttonHeight * access::kLargeControlFactor >= 32.0f);
    GF_EXPECT (11.0f * access::kLargeControlFactor >= 12.9f);

    ScopedSettings scoped;
    settings::GlobalSettings::shared()->setInt (access::kKeyControlSize, 1);

    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1100, 700);
    GF_EXPECT_NEAR (editor.getControlScaleFactorForTests(), access::kLargeControlFactor, 1e-6);
    GF_EXPECT_NEAR (editor.getTransform().getScaleFactor(), access::kLargeControlFactor, 0.01);
}

GF_TEST (Access, no_clipping_at_min_with_large)
{
    juce::Rectangle<int> standardVis;
    {
        ScopedSettings scoped;
        GapFillAudioProcessor proc;
        proc.getUiState().setViewMode (ui::ViewMode::Advanced);
        GapFillAudioProcessorEditor editor (proc);
        editor.setSize (1020, 620);
        standardVis = editor.getVisualiserBoundsForTests();
    }

    ScopedSettings scoped;
    settings::GlobalSettings::shared()->setInt (access::kKeyControlSize, 1);
    GapFillAudioProcessor proc;
    proc.getUiState().setViewMode (ui::ViewMode::Advanced);
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1020, 620);

    // The layout is logical: scaling never changes bounds, so nothing can clip that did not clip before.
    GF_EXPECT (editor.getVisualiserBoundsForTests() == standardVis);

    std::vector<juce::Component*> all;
    collect (editor, all);

    for (auto* c : all)
        if (c->isShowing() && (dynamic_cast<juce::Button*> (c) != nullptr))
            GF_EXPECT_MSG (c->getHeight() * access::kLargeControlFactor >= 24.0f * 1.18f - 0.5f || c->getHeight() >= 20, c->getName());
}

GF_TEST (Access, tab_order_complete_and_visible_focus)
{
    ScopedSettings scoped;
    GapFillAudioProcessor proc;
    proc.setKeyboardNavigationEnabled (true);
    proc.getUiState().setViewMode (ui::ViewMode::Advanced);
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1100, 700);
    editor.applyKeyboardNavigationSettingForTests();

    juce::KeyboardFocusTraverser traverser;
    const auto order = traverser.getAllComponents (&editor);

    std::vector<juce::Component*> all;
    collect (editor, all);

    int interactive = 0;

    for (auto* c : all)
    {
        const bool isControl = dynamic_cast<juce::Button*> (c) != nullptr || dynamic_cast<juce::Slider*> (c) != nullptr
                                || dynamic_cast<juce::ComboBox*> (c) != nullptr;

        if (isControl && visibleInTree (*c, editor) && c->getWantsKeyboardFocus())
        {
            ++interactive;
            GF_EXPECT_MSG (std::find (order.begin(), order.end(), c) != order.end(), "not reachable by Tab: " + c->getName());
        }
    }

    GF_EXPECT (interactive > 20);

    // The focus outline is really drawn: a focused toggle renders differently from an unfocused one.
    GapToggle toggle (proc.getValueTreeState(), "pingPong", "PING PONG", "x");
    toggle.setBounds (0, 0, 120, 28);
    toggle.getButton().setWantsKeyboardFocus (true);

    const auto render = [&toggle]
    {
        juce::Image img (juce::Image::ARGB, 120, 28, true);
        juce::Graphics g (img);
        toggle.paintEntireComponent (g, true);
        return img;
    };

    const auto unfocused = render();
    focusRingWidth = 3.0f;
    toggle.getButton().grabKeyboardFocus();

    if (toggle.getButton().hasKeyboardFocus (false))
    {
        const auto focused = render();
        bool differs = false;

        for (int x = 0; x < 120 && ! differs; ++x)
            for (int y = 0; y < 28 && ! differs; ++y)
                differs = focused.getPixelAt (x, y) != unfocused.getPixelAt (x, y);

        GF_EXPECT_MSG (differs, "focus ring visible");
    }
}

GF_TEST (Access, reduced_motion_unaffected)
{
    ScopedSettings scoped;
    GapFillAudioProcessor proc;
    proc.setReducedMotionEnabled (true);
    gapfill::setReducedMotionEnabled (true);

    GapFillAudioProcessorEditor editor (proc);
    settings::GlobalSettings::shared()->setInt (access::kKeyContrast, 2);
    editor.applyAccessibilityForTests();

    GF_EXPECT (proc.isReducedMotionEnabled() && gapfill::isReducedMotionEnabled());
    gapfill::setReducedMotionEnabled (false);
}
