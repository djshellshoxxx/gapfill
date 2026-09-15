#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"

namespace gapfill
{

//==============================================================================
/** Rotary slider with shift-for-fine dragging and double-click-to-default. */
class FineSlider : public juce::Slider
{
public:
    FineSlider();
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
};

//==============================================================================
/** A labelled knob bound to an APVTS parameter. */
class GapKnob : public juce::Component
{
public:
    GapKnob (juce::AudioProcessorValueTreeState& state,
             const juce::String& parameterID,
             const juce::String& displayName,
             const juce::String& tooltip,
             juce::Colour accent,
             bool large = false);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider& getSlider() noexcept { return slider; }
    void setAccentColour (juce::Colour c);

private:
    void refreshValueText();

    juce::RangedAudioParameter* parameter = nullptr;
    FineSlider slider;
    juce::Label nameLabel, valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool large = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapKnob)
};

//==============================================================================
/** A labelled combo box bound to a choice parameter. */
class GapCombo : public juce::Component
{
public:
    GapCombo (juce::AudioProcessorValueTreeState& state,
              const juce::String& parameterID,
              const juce::String& displayName,
              const juce::String& tooltip);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::ComboBox& getComboBox() noexcept { return combo; }

private:
    juce::ComboBox combo;
    juce::Label nameLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapCombo)
};

//==============================================================================
/** A toggle button bound to a bool parameter. */
class GapToggle : public juce::Component
{
public:
    GapToggle (juce::AudioProcessorValueTreeState& state,
               const juce::String& parameterID,
               const juce::String& displayName,
               const juce::String& tooltip,
               juce::Colour accent = colours::accent);

    void resized() override;

    juce::TextButton& getButton() noexcept { return button; }

private:
    juce::TextButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapToggle)
};

//==============================================================================
/** A compact horizontal slider with a caption, used in dense lists. */
class GapBar : public juce::Component
{
public:
    GapBar (juce::AudioProcessorValueTreeState& state,
            const juce::String& parameterID,
            const juce::String& displayName,
            const juce::String& tooltip,
            juce::Colour accent);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void refreshValueText();

    juce::RangedAudioParameter* parameter = nullptr;
    FineSlider slider;
    juce::Label nameLabel, valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapBar)
};

} // namespace gapfill
