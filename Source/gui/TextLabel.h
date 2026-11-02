#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace gapfill
{
/** Read-only, wrapped label whose explicit font is shared by measurement and
    painting. Keeps Label's text/accessibility contract while avoiding a global
    LookAndFeel deriving a different font from the measured component height. */
class TextLabel final : public juce::Label
{
public:
    using juce::Label::Label;

    int heightForWidth (int width) const
    {
        juce::TextLayout layout;
        layoutText (layout, juce::jmax (1, width - getBorderSize().getLeftAndRight()), juce::Colours::white);
        return (int) std::ceil (layout.getHeight()) + getBorderSize().getTopAndBottom() + 2;
    }

    void paint (juce::Graphics& graphics) override
    {
        graphics.fillAll (findColour (juce::Label::backgroundColourId));
        juce::TextLayout layout;
        const auto area = getBorderSize().subtractedFrom (getLocalBounds()).toFloat();
        layoutText (layout, juce::jmax (1.0f, area.getWidth()),
                    findColour (juce::Label::textColourId).withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));
        layout.draw (graphics, area);
        graphics.setColour (findColour (juce::Label::outlineColourId));
        graphics.drawRect (getLocalBounds());
    }

private:
    void layoutText (juce::TextLayout& layout, float width, juce::Colour colour) const
    {
        juce::AttributedString text;
        text.setJustification (getJustificationType());
        text.append (getText(), getFont(), colour);
        layout.createLayout (text, width);
    }
};
} // namespace gapfill
