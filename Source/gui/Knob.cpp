#include "Knob.h"

namespace gapfill
{

namespace
{
    constexpr int kCoarseSensitivity = 240;
    constexpr int kFineSensitivity   = 2200;
}

//==============================================================================
FineSlider::FineSlider()
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity (kCoarseSensitivity);
    setWantsKeyboardFocus (false);
    setScrollWheelEnabled (true);
}

void FineSlider::mouseDown (const juce::MouseEvent& e)
{
    // Shift (or ctrl/cmd) gives a much finer drag for precise tweaks.
    setMouseDragSensitivity (e.mods.isShiftDown() || e.mods.isCommandDown()
                                ? kFineSensitivity : kCoarseSensitivity);
    juce::Slider::mouseDown (e);
}

void FineSlider::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
    setMouseDragSensitivity (kCoarseSensitivity);
}

//==============================================================================
GapKnob::GapKnob (juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterID,
                  const juce::String& displayName,
                  const juce::String& tooltip,
                  juce::Colour accent,
                  bool isLarge)
    : large (isLarge)
{
    parameter = state.getParameter (parameterID);

    addAndMakeVisible (slider);
    slider.setColour (juce::Slider::thumbColourId, accent);
    slider.setTooltip (tooltip);

    if (parameter != nullptr)
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    slider.onValueChange = [this] { refreshValueText(); };

    nameLabel.setText (displayName.toUpperCase(), juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setColour (juce::Label::textColourId, colours::textDim);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setColour (juce::Label::textColourId, colours::text);
    valueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (valueLabel);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterID, slider);
    refreshValueText();
}

void GapKnob::setAccentColour (juce::Colour c)
{
    slider.setColour (juce::Slider::thumbColourId, c);
    repaint();
}

void GapKnob::refreshValueText()
{
    if (parameter != nullptr)
        valueLabel.setText (parameter->getCurrentValueAsText(), juce::dontSendNotification);
}

void GapKnob::resized()
{
    auto b = getLocalBounds();
    const auto nameHeight = large ? 16 : 13;
    const auto valueHeight = large ? 17 : 14;

    nameLabel.setBounds (b.removeFromTop (nameHeight));
    valueLabel.setBounds (b.removeFromBottom (valueHeight));

    const auto size = juce::jmin (b.getWidth(), b.getHeight());
    slider.setBounds (b.withSizeKeepingCentre (size, size));

    nameLabel.setFont (juce::Font (juce::FontOptions ((float) nameHeight * 0.78f)).withExtraKerningFactor (0.1f));
    valueLabel.setFont (juce::Font (juce::FontOptions ((float) valueHeight * 0.82f)));
}

void GapKnob::paint (juce::Graphics&) {}

//==============================================================================
GapCombo::GapCombo (juce::AudioProcessorValueTreeState& state,
                    const juce::String& parameterID,
                    const juce::String& displayName,
                    const juce::String& tooltip)
{
    nameLabel.setText (displayName.toUpperCase(), juce::dontSendNotification);
    nameLabel.setColour (juce::Label::textColourId, colours::textDim);
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (nameLabel);

    combo.setTooltip (tooltip);
    combo.setWantsKeyboardFocus (false);
    addAndMakeVisible (combo);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, parameterID, combo);
}

void GapCombo::resized()
{
    auto b = getLocalBounds();
    nameLabel.setBounds (b.removeFromTop (14));
    nameLabel.setFont (juce::Font (juce::FontOptions (10.5f)).withExtraKerningFactor (0.12f));
    combo.setBounds (b.reduced (0, 1));
}

void GapCombo::paint (juce::Graphics&) {}

//==============================================================================
GapToggle::GapToggle (juce::AudioProcessorValueTreeState& state,
                      const juce::String& parameterID,
                      const juce::String& displayName,
                      const juce::String& tooltip,
                      juce::Colour accent)
{
    button.setButtonText (displayName);
    button.setClickingTogglesState (true);
    button.setTooltip (tooltip);
    button.setWantsKeyboardFocus (false);
    button.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.8f));
    addAndMakeVisible (button);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterID, button);
}

void GapToggle::resized()
{
    button.setBounds (getLocalBounds());
}

//==============================================================================
GapBar::GapBar (juce::AudioProcessorValueTreeState& state,
                const juce::String& parameterID,
                const juce::String& displayName,
                const juce::String& tooltip,
                juce::Colour accent)
{
    parameter = state.getParameter (parameterID);

    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setColour (juce::Slider::thumbColourId, accent);
    slider.setTooltip (tooltip);
    slider.onValueChange = [this] { refreshValueText(); };

    if (parameter != nullptr)
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    addAndMakeVisible (slider);

    nameLabel.setText (displayName, juce::dontSendNotification);
    nameLabel.setColour (juce::Label::textColourId, colours::textDim);
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    valueLabel.setColour (juce::Label::textColourId, colours::text);
    valueLabel.setJustificationType (juce::Justification::centredRight);
    valueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (valueLabel);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterID, slider);
    refreshValueText();
}

void GapBar::refreshValueText()
{
    if (parameter != nullptr)
        valueLabel.setText (parameter->getCurrentValueAsText(), juce::dontSendNotification);
}

void GapBar::resized()
{
    auto b = getLocalBounds();
    auto top = b.removeFromTop (juce::jmax (12, b.getHeight() / 2));
    valueLabel.setBounds (top.removeFromRight (juce::jmin (78, top.getWidth() / 2)));
    nameLabel.setBounds (top);
    nameLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    valueLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    slider.setBounds (b);
}

void GapBar::paint (juce::Graphics&) {}

} // namespace gapfill
