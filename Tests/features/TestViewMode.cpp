// U-5 / F-M04 Simple / Advanced view.

#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "features/viewmode/ViewMode.h"
#include "UiTestUtil.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    juce::MemoryBlock stateOf (GapFillAudioProcessor& p)
    {
        juce::MemoryBlock block;
        p.getStateInformation (block);
        return block;
    }

    void collect (juce::Component& root, std::vector<juce::Component*>& out)
    {
        for (auto* c : root.getChildren())
        {
            out.push_back (c);
            collect (*c, out);
        }
    }

    juce::MemoryBlock withoutViewMode (const juce::MemoryBlock& state)
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        xml->removeAttribute ("viewMode");
        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary (*xml, out);
        return out;
    }
}

GF_TEST (ViewMode, rule_table_covers_the_documented_groups)
{
    using M = ui::ViewMode;
    for (const char* id : { "tab.weights", "tab.spectral", "creative.advancedRows", "detection.rest", "protection.rest" })
        GF_EXPECT_MSG (! viewmode::isVisible (id, M::Simple) && viewmode::isVisible (id, M::Advanced), id);

    for (const char* id : { "creative.macros", "creative.fillType", "creative.extra", "mix", "visualiser", "header", "unknown.id" })
        GF_EXPECT_MSG (viewmode::isVisible (id, M::Simple) && viewmode::isVisible (id, M::Advanced), id);
}

GF_TEST (ViewMode, new_instance_simple)
{
    GapFillAudioProcessor proc;
    GF_EXPECT (proc.getUiState().getViewMode() == ui::ViewMode::Simple);
}

GF_TEST (ViewMode, view_mode_roundtrip)
{
    for (const auto mode : { ui::ViewMode::Simple, ui::ViewMode::Advanced })
    {
        GapFillAudioProcessor a;
        a.getUiState().setViewMode (mode);
        const auto state = stateOf (a);

        GapFillAudioProcessor b;
        b.getUiState().setViewMode (mode == ui::ViewMode::Simple ? ui::ViewMode::Advanced : ui::ViewMode::Simple);
        b.setStateInformation (state.getData(), (int) state.getSize());
        GF_EXPECT (b.getUiState().getViewMode() == mode);
    }
}

GF_TEST (ViewMode, fresh_instance_state_roundtrips_as_simple)
{
    GapFillAudioProcessor fresh;
    const auto state = stateOf (fresh);              // what a host stores/replays on instantiate

    GapFillAudioProcessor other;
    other.setStateInformation (state.getData(), (int) state.getSize());
    GF_EXPECT (other.getUiState().getViewMode() == ui::ViewMode::Simple);
}

GF_TEST (ViewMode, old_state_opens_advanced)
{
    GapFillAudioProcessor src;
    const auto old = withoutViewMode (stateOf (src));        // no viewMode property: saved before U-5

    GapFillAudioProcessor proc;
    GF_EXPECT (proc.getUiState().getViewMode() == ui::ViewMode::Simple);
    proc.setStateInformation (old.getData(), (int) old.getSize());
    GF_EXPECT (proc.getUiState().getViewMode() == ui::ViewMode::Advanced);
}

GF_TEST (ViewMode, corrupt_viewmode_value_defaults)
{
    GapFillAudioProcessor src;
    const auto good = stateOf (src);
    auto xml = juce::AudioProcessor::getXmlFromBinary (good.getData(), (int) good.getSize());
    xml->setAttribute ("viewMode", "banana");
    juce::MemoryBlock bad;
    juce::AudioProcessor::copyXmlToBinary (*xml, bad);

    GapFillAudioProcessor proc;
    proc.setStateInformation (bad.getData(), (int) bad.getSize());
    GF_EXPECT (proc.getUiState().getViewMode() == ui::ViewMode::Simple);   // unchanged default, no crash

    xml->setAttribute ("viewMode", 7);
    proc.getUiState().setViewMode (ui::ViewMode::Advanced);
    juce::MemoryBlock outOfRange;
    juce::AudioProcessor::copyXmlToBinary (*xml, outOfRange);
    proc.setStateInformation (outOfRange.getData(), (int) outOfRange.getSize());
    GF_EXPECT (proc.getUiState().getViewMode() == ui::ViewMode::Advanced);
}

GF_TEST (ViewMode, hidden_controls_keep_parameters)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "seed", 1234.0f }, { "minGap", 222.0f }, { "protLow", 333.0f }, { "wStutter", 11.0f }, { "xoverMH", 4000.0f } });

    auto snapshot = [&proc]
    {
        std::vector<float> v;
        for (const auto& id : allParameterIDs())
            v.push_back (proc.getValueTreeState().getRawParameterValue (id)->load());
        return v;
    };

    const auto before = snapshot();
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1100, 700);

    juce::Random rng (7);
    for (int i = 0; i < 50; ++i)
    {
        proc.getUiState().setViewMode (rng.nextBool() ? ui::ViewMode::Simple : ui::ViewMode::Advanced);
        editor.applyViewModeForTests();
    }

    GF_EXPECT (snapshot() == before);

    // Hidden controls still follow automation.
    proc.getUiState().setViewMode (ui::ViewMode::Simple);
    editor.applyViewModeForTests();
    setParams (proc, { { "seed", 4321.0f } });
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("seed")->load(), 4321.0, 0.01);
}

GF_TEST (ViewMode, current_tab_falls_back_to_creative_and_returns)
{
    GapFillAudioProcessor proc;
    proc.getUiState().setViewMode (ui::ViewMode::Advanced);
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1100, 700);

    editor.selectTabForTests (1);                              // AUTO WEIGHTS
    GF_EXPECT (editor.isWeightsPanelShownForTests());

    proc.getUiState().setViewMode (ui::ViewMode::Simple);
    editor.applyViewModeForTests();
    GF_EXPECT (! editor.isWeightsPanelShownForTests() && editor.isCreativePanelShownForTests());
    GF_EXPECT (! editor.isTabVisibleForTests (1) && ! editor.isTabVisibleForTests (2) && editor.isTabVisibleForTests (0));

    proc.getUiState().setViewMode (ui::ViewMode::Advanced);
    editor.applyViewModeForTests();
    GF_EXPECT_MSG (editor.isWeightsPanelShownForTests(), "the previous tab is restored with Advanced");
    GF_EXPECT (editor.isTabVisibleForTests (1) && editor.isTabVisibleForTests (2));
}

GF_TEST (ViewMode, keyboard_focus_only_visible_controls)
{
    GapFillAudioProcessor proc;
    proc.setKeyboardNavigationEnabled (true);
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1020, 620);
    editor.applyKeyboardNavigationSettingForTests();

    const auto focusable = [&editor]
    {
        juce::KeyboardFocusTraverser t;
        return (int) t.getAllComponents (&editor).size();
    };

    proc.getUiState().setViewMode (ui::ViewMode::Advanced);
    editor.applyViewModeForTests();
    const auto advancedCount = focusable();

    proc.getUiState().setViewMode (ui::ViewMode::Simple);
    editor.applyViewModeForTests();
    const auto simpleCount = focusable();

    GF_EXPECT_MSG (simpleCount < advancedCount, "hidden controls leave the Tab order");

    juce::KeyboardFocusTraverser t;
    for (auto* c : t.getAllComponents (&editor))
        GF_EXPECT_MSG (visibleInTree (*c, editor), c->getName());
}

GF_TEST (ViewMode, simple_has_no_clipped_controls_at_min_size)
{
    GapFillAudioProcessor proc;
    proc.getUiState().setViewMode (ui::ViewMode::Simple);
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1020, 620);

    std::vector<juce::Component*> all;
    collect (editor, all);

    int knobs = 0;

    for (auto* c : all)
    {
        if (dynamic_cast<GapKnob*> (c) == nullptr || ! visibleInTree (*c, editor))
            continue;

        ++knobs;
        const auto inEditor = editor.getLocalArea (c, c->getLocalBounds());
        GF_EXPECT_MSG (editor.getLocalBounds().contains (inEditor), c->getProperties()[kParamIdProperty].toString());
        GF_EXPECT_MSG (c->getWidth() >= 48 && c->getHeight() >= 48, c->getProperties()[kParamIdProperty].toString());

        if (auto* parent = c->getParentComponent())
            GF_EXPECT_MSG (parent->getLocalBounds().contains (c->getBounds()), c->getProperties()[kParamIdProperty].toString());
    }

    // 5 macros + Sensitivity + Transient Protect + Dry/Wet/Output.
    GF_EXPECT_MSG (knobs >= 10, "visible knobs: " + juce::String (knobs));

    // The toggle sits in the tab row, right of the tabs, inside the editor.
    const auto toggle = editor.getViewToggleBoundsForTests();
    GF_EXPECT (! toggle.isEmpty() && editor.getLocalBounds().contains (toggle));
    GF_EXPECT (! toggle.intersects (editor.getTabBoundsForTests (0)));
    GF_EXPECT (toggle.getWidth() >= 130);
}

GF_TEST (ViewMode, advanced_toggle_does_not_overlap_tabs_at_min_size)
{
    GapFillAudioProcessor proc;
    proc.getUiState().setViewMode (ui::ViewMode::Advanced);
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1020, 620);

    const auto toggle = editor.getViewToggleBoundsForTests();
    GF_EXPECT (toggle.getWidth() >= 130);

    for (int i = 0; i < 3; ++i)
    {
        const auto tab = editor.getTabBoundsForTests (i);
        GF_EXPECT (! tab.isEmpty() && ! tab.intersects (toggle) && tab.getWidth() >= 100);
    }
}
