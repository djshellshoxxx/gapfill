#pragma once

#include "PluginProcessor.h"
#include "gui/LookAndFeel.h"
#include "gui/Knob.h"
#include "gui/Panels.h"
#include "gui/WaveformGapView.h"
#include "gui/HelpView.h"

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
    void setTab (Tab newTab);
    void refreshPresetList();
    void presetSelected();
    void saveUserPreset();
    void toggleAB();
    void copyToOtherSlot();
    static juce::File userPresetDirectory();

    GapFillAudioProcessor& proc;

    GapFillLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { nullptr, 600 };

    // --- top bar ---
    juce::Label logo, tagline, latencyLabel;
    juce::ComboBox presetBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, savePreset { "SAVE" };
    juce::TextButton abButton { "A" }, copyButton { "COPY" };
    juce::TextButton randomButton { "RANDOM" }, randomAllButton { "RANDOM ALL" };
    juce::TextButton resetButton { "RESET" }, helpButton { "HELP" };
    juce::TextButton bypassButton { "BYPASS" };
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

    // --- A/B ---
    juce::ValueTree slotA, slotB;
    bool usingSlotB = false;

    juce::Array<juce::File> userPresetFiles;
    juce::String pendingPresetName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapFillAudioProcessorEditor)
};

} // namespace gapfill
