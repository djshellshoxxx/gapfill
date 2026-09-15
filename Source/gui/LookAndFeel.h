#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace gapfill
{

//==============================================================================
/** The GapFill palette. Kept in one place so every panel agrees. */
namespace colours
{
    const juce::Colour background   { 0xff0d0f14 };
    const juce::Colour panel        { 0xff161a22 };
    const juce::Colour panelRaised  { 0xff1d222c };
    const juce::Colour outline      { 0xff2a323f };
    const juce::Colour text         { 0xffd9e1ec };
    const juce::Colour textDim      { 0xff7f8b9d };
    const juce::Colour accent       { 0xff4fe3f0 };   // detection / gaps
    const juce::Colour accentWarm   { 0xffff5fa2 };   // fills
    const juce::Colour accentAmber  { 0xffffc24d };   // protection
    const juce::Colour accentGreen  { 0xff6ee79b };
    const juce::Colour waveform     { 0xff5c6a80 };
    const juce::Colour gapFillTint  { 0x334fe3f0 };

    inline juce::Colour bandColour (int band)
    {
        switch (band)
        {
            case 0:  return juce::Colour (0xffff8a4d);   // low
            case 1:  return juce::Colour (0xff6ee79b);   // mid
            default: return juce::Colour (0xff4fe3f0);   // high
        }
    }
}

//==============================================================================
class GapFillLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GapFillLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    /** Shared drawing helper for the framed panels used everywhere. */
    static void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds,
                           const juce::String& title, juce::Colour tint);
};

} // namespace gapfill
