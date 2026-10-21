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
#include "features/info/HoverStatus.h"
#include "features/viewmode/ViewMode.h"
#include "features/tour/Tour.h"
#include "features/access/Access.h"
#include "features/calibrate/Gui.h"
#include "features/presetbrowser/PresetBrowser.h"

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
    void setScaleFactor (float hostScale) override;       // U-8: composed with the user's Interface size

public:
    /** U-7: shows the quick tour (force = also when it was finished or suppressed). */
    void showTour (bool force);
    static bool& tourAutoShowEnabled();

   #ifdef GAPFILL_UNIT_TESTS
    void applyKeyboardNavigationSettingForTests() { applyKeyboardNavigationSetting(); }
    int getKeyboardFocusableControlCountForTests() const;
    int getUnnamedKeyboardFocusableControlCountForTests() const;
    void applyViewModeForTests()                { applyViewMode(); }
    bool isTabVisibleForTests (int i) const     { return ! getTabBoundsForTests (i).isEmpty(); }
    DetectionPanel& getDetectionPanelForTests() { return detectionPanel; }
    void selectTabForTests (int i)              { setTab ((Tab) juce::jlimit (0, 3, i)); }
    bool isWeightsPanelShownForTests() const    { return weightsPanel.isVisible(); }
    bool isCreativePanelShownForTests() const   { return creativePanel.isVisible(); }
    juce::Rectangle<int> getViewToggleBoundsForTests() const { return slots->getSlotBounds (Slot::TopBarRight); }
    int getAppliedScalePercentForTests() const noexcept { return appliedScalePercent; }
    void applyScaleForTests()                   { applyScale(); }
    juce::Rectangle<int> getOptionsExtraBoundsForTests() const { return optionsView.getExtraBounds(); }
    juce::Rectangle<int> getOptionsBoundsForTests() const { return optionsView.getBounds(); }
    tour::TourOverlay* getTourOverlayForTests() noexcept { return tourOverlay.get(); }
    bool maybeAutoShowTourForTests()            { return maybeAutoShowTour(); }
    void applyAccessibilityForTests()           { applyAccessibility(); }
    float getControlScaleFactorForTests() const noexcept { return controlScaleFactor; }
    calibrate::CalibrationSession* getCalibrationForTests() noexcept { return calibration.get(); }
    calibrate::CalibrateCard* getCalibrateCardForTests() noexcept { return calibrateCard.get(); }
    juce::TextButton& getLearnButtonForTests() noexcept { return detectionPanel.getLearnButton(); }
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
    void applyViewMode();
    void applyScale();
    void applyAccessibility();
    juce::Component* findTourTarget (const juce::String& id);
    bool maybeAutoShowTour();
    void refreshPresetList();
    void setPresetDisplay (const juce::String&);
    int  currentPresetIndex() const;
    void stepPreset (int delta);
    bool loadCatalogEntry (const presets::PresetEntry&);
    void showPresetBrowser();
    void showMutationLab();
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
    info::HoverStatus hoverStatus;
    std::shared_ptr<settings::GlobalSettings> settingsFile;   // per-user global settings (U-4/U-8/U-7/F-D04)      // U-9: "Name value (base -> effective)" in the footer
    // Destroyed rather than hidden when the user turns tooltips off, so no
    // stray window can appear.
    std::unique_ptr<juce::TooltipWindow> tooltips;

    // --- top bar ---
    juce::Label logo, tagline, latencyLabel;
    juce::TextButton presetBox;     // shows the current preset name; opens the browser (U-4)
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
    float controlScaleFactor = 1.0f;     // F-D04 Large controls, composed into the scale path
    int accessSignature = -1;
    float hostScaleValue = 1.0f;
    int lastRequestedScale = 100, appliedScalePercent = 100;
    ui::ViewMode appliedView = ui::ViewMode::Advanced;
    std::optional<Tab> tabBeforeSimple;
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

    presets::PresetCatalog navCatalog;     // factory + user entries for the < and > buttons

    // The file "Save" writes to. Empty until the user has saved or opened one.
    juce::File currentPresetFile;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String pendingPresetName;

    // U-7 first-run guide; declared last so it is destroyed first.
    std::unique_ptr<calibrate::CalibrationSession> calibration;
    std::unique_ptr<calibrate::CalibrateCard> calibrateCard;
    std::unique_ptr<tour::TourModel> tourModel;
    std::unique_ptr<tour::TourOverlay> tourOverlay;

    bool keyboardNavigationApplied = false;
    bool lastKeyboardNavigationState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapFillAudioProcessorEditor)
};

} // namespace gapfill
