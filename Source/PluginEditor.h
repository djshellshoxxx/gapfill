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

private:
    enum class Tab { Creative = 0, Weights, Spectral };

    void timerCallback() override;
    juce::Rectangle<int> footerBounds() const;
    void setTab (Tab newTab);
    void refreshPresetList();
    void presetSelected();
    void saveUserPreset();
    void showFileMenu();
    void savePresetToFile (const juce::File&);
    void loadPresetFromFile (const juce::File&);
    void doSave();
    void doSaveAs();
    void doOpen();
    void showOptions();
    void applyTooltipSetting();
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

    juce::TextButton tabCreative { "CREATIVE" }, tabWeights { "AUTO WEIGHTS" }, tabSpectral { "SPECTRAL" };
    Tab currentTab = Tab::Creative;

    HelpView helpView;
    OptionsView optionsView;

    // --- A/B ---
    juce::ValueTree slotA, slotB;
    bool usingSlotB = false;

    juce::Array<juce::File> userPresetFiles;

    // The file "Save" writes to. Empty until the user has saved or opened one.
    juce::File currentPresetFile;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String pendingPresetName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapFillAudioProcessorEditor)
};

} // namespace gapfill
