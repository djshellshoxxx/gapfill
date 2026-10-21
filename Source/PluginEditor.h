// GapFill
// Copyright © 2026 Sheldon Davidson.
// Licensed under AGPL-3.0-or-later. See LICENSE.
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "PluginProcessor.h"
#include "gui/LookAndFeel.h"
#include "gui/Knob.h"
#include "gui/Panels.h"
#include "gui/WaveformGapView.h"
#include "gui/HelpView.h"
#include "gui/OptionsView.h"
#include "gui/SlotHost.h"
#include "gui/SlotPanels.h"

namespace gapfill
{

//==============================================================================
class GapFillAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit GapFillAudioProcessorEditor (GapFillAudioProcessor&);
    ~GapFillAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

   #ifdef GAPFILL_UNIT_TESTS
    void applyKeyboardNavigationSettingForTests() { applyKeyboardNavigationSetting(); }
    int getKeyboardFocusableControlCountForTests() const;
    int getUnnamedKeyboardFocusableControlCountForTests() const;
    void selectPerformanceForTests()            { setTab (Tab::Performance); }
    bool isPerformanceTabForTests() const noexcept { return performanceAsTab; }
    bool isPerformanceShownForTests() const noexcept { return performancePanel != nullptr && performancePanel->isVisible(); }
    StackPanel* getPerformancePanelForTests() noexcept { return performancePanel.get(); }
    EffectsDrawer* getEffectsDrawerForTests() noexcept { return effectsDrawer.get(); }
    juce::Rectangle<int> getVisualiserBoundsForTests() const { return visualiser.getBounds(); }
    juce::Rectangle<int> getTabBoundsForTests (int index) const
    {
        const juce::Component* const tabs[] = { &tabCreative, &tabWeights, &tabSpectral, &tabPerformance };
        return tabs[juce::jlimit (0, 3, index)]->isVisible() ? tabs[juce::jlimit (0, 3, index)]->getBounds()
                                                              : juce::Rectangle<int>();
    }
   #endif

private:
    enum class Tab { Creative = 0, Weights, Spectral, Performance };

    void timerCallback() override;
    juce::Rectangle<int> footerBounds() const;
    void setTab (Tab newTab);
    void refreshPresetList();
    void presetSelected();
    void saveUserPreset();
    void showFileMenu();
    bool savePresetToFile (const juce::File&);
    void loadPresetFromFile (const juce::File&);
    void doSave();
    void doSaveAs();
    void doOpen();
    void showOptions();
    void applyTooltipSetting();
    void applyKeyboardNavigationSetting();
    void toggleAB();
    void copyToOtherSlot();
    static juce::File userPresetDirectory();

    GapFillAudioProcessor& proc;

    GapFillLookAndFeel lookAndFeel;
    // Destroyed rather than hidden when the user turns tooltips off, so no
    // stray window can appear.
    std::unique_ptr<juce::TooltipWindow> tooltips;

    // --- top bar ---
    juce::Label logo, tagline, latencyLabel;
    juce::ComboBox presetBox;
    // Momentary actions flash the accent for 100ms; abButton and bypassButton
    // are stateful, so they show their state instead.
    FlashButton prevPreset { "<" }, nextPreset { ">" }, fileButton { "FILE" };
    juce::TextButton abButton { "A" };
    FlashButton copyButton { "COPY" };
    FlashButton randomButton { "RANDOM" }, randomAllButton { "RANDOM ALL" };
    FlashButton resetButton { "RESET" }, helpButton { "HELP" };
    FlashButton surpriseButton { "SURPRISE" }, undoButton { "UNDO" };
    // A ParamButton rather than a TextButton so bypass gets the same
    // right-click menu - and so it can be mapped to a CC - as every other
    // parameter-backed control.
    ParamButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // --- main areas ---
    WaveformGapView visualiser;
    DetectionPanel detectionPanel;
    CreativePanel creativePanel;
    WeightsPanel weightsPanel;
    SpectralPanel spectralPanel;
    ProtectionPanel protectionPanel;
    MixPanel mixPanel;

    juce::TextButton tabCreative { "CREATIVE" }, tabWeights { "AUTO WEIGHTS" }, tabSpectral { "SPECTRAL" },
                     tabPerformance { "PERFORMANCE" };
    Tab currentTab = Tab::Creative;
    bool performanceAsTab = false;      ///< else reached from the FILE menu (UI-0 tab plan)

    HelpView helpView;
    OptionsView optionsView;

    // Feature-owned components (docs/dev/02 F0-9); created after the panels so they sit on top.
    std::unique_ptr<SlotHost> slots;

    // UI-0 containers for the Stack slots. Declared after `slots` so they are
    // destroyed first; null when no feature registered a component for the slot.
    std::unique_ptr<StackPanel> performancePanel;
    std::unique_ptr<EffectsDrawer> effectsDrawer;

    // --- A/B ---
    juce::ValueTree slotA, slotB;
    bool usingSlotB = false;

    juce::Array<juce::File> userPresetFiles;

    // The file "Save" writes to. Empty until the user has saved or opened one.
    juce::File currentPresetFile;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String pendingPresetName;

    bool keyboardNavigationApplied = false;
    bool lastKeyboardNavigationState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapFillAudioProcessorEditor)
};

} // namespace gapfill
