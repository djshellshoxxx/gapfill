#include "LookAndFeel.h"

namespace gapfill
{

GapFillLookAndFeel::GapFillLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::Label::textColourId,                 colours::text);
    setColour (juce::Slider::textBoxTextColourId,         colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::Slider::thumbColourId,               colours::accent);
    setColour (juce::ComboBox::backgroundColourId,        colours::panelRaised);
    setColour (juce::ComboBox::textColourId,              colours::text);
    setColour (juce::ComboBox::outlineColourId,           colours::outline);
    setColour (juce::ComboBox::arrowColourId,             colours::textDim);
    setColour (juce::PopupMenu::backgroundColourId,       colours::panelRaised);
    setColour (juce::PopupMenu::textColourId,             colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId,  juce::Colours::white);
    setColour (juce::TextButton::buttonColourId,          colours::panelRaised);
    setColour (juce::TextButton::buttonOnColourId,        colours::accent.withAlpha (0.55f));
    setColour (juce::TextButton::textColourOffId,         colours::text);
    setColour (juce::TextButton::textColourOnId,          juce::Colours::black);
    setColour (juce::TooltipWindow::backgroundColourId,   juce::Colour (0xf2101419));
    setColour (juce::TooltipWindow::textColourId,         colours::text);
    setColour (juce::TooltipWindow::outlineColourId,      colours::outline);
    setColour (juce::ScrollBar::thumbColourId,            colours::outline.brighter (0.4f));
}

//==============================================================================
void GapFillLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                           juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto thickness = juce::jmax (2.5f, radius * 0.17f);
    const auto arcRadius = radius - thickness * 0.5f;

    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto accentColour = slider.findColour (juce::Slider::thumbColourId);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (colours::outline);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    if (slider.isEnabled())
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, angle, true);
        g.setColour (accentColour.withAlpha (0.25f));
        g.strokePath (value, juce::PathStrokeType (thickness * 2.1f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour (accentColour);
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // Inner face
    const auto inner = arcRadius - thickness * 0.85f;
    g.setColour (colours::panelRaised.brighter (0.05f));
    g.fillEllipse (juce::Rectangle<float> (inner * 2.0f, inner * 2.0f).withCentre (centre));
    g.setColour (colours::outline.withAlpha (0.8f));
    g.drawEllipse (juce::Rectangle<float> (inner * 2.0f, inner * 2.0f).withCentre (centre), 1.0f);

    // Pointer
    juce::Path pointer;
    const auto pw = juce::jmax (2.0f, radius * 0.09f);
    pointer.addRoundedRectangle (-pw * 0.5f, -inner * 0.92f, pw, inner * 0.55f, pw * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
    g.setColour (slider.isEnabled() ? accentColour.brighter (0.3f) : colours::textDim);
    g.fillPath (pointer);
}

void GapFillLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float, float,
                                           juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto accentColour = slider.findColour (juce::Slider::thumbColourId);

    if (style == juce::Slider::LinearHorizontal)
    {
        const auto cy = (float) y + (float) height * 0.5f;
        const auto h = juce::jmax (4.0f, (float) height * 0.28f);
        juce::Rectangle<float> track ((float) x, cy - h * 0.5f, (float) width, h);

        g.setColour (colours::outline);
        g.fillRoundedRectangle (track, h * 0.5f);

        auto filled = track.withWidth (juce::jmax (0.0f, sliderPos - (float) x));
        g.setColour (accentColour.withAlpha (0.85f));
        g.fillRoundedRectangle (filled, h * 0.5f);

        g.setColour (accentColour.brighter (0.4f));
        g.fillEllipse (juce::Rectangle<float> (h * 1.9f, h * 1.9f).withCentre ({ sliderPos, cy }));
    }
    else
    {
        const auto cx = (float) x + (float) width * 0.5f;
        const auto w = juce::jmax (4.0f, (float) width * 0.28f);
        juce::Rectangle<float> track (cx - w * 0.5f, (float) y, w, (float) height);

        g.setColour (colours::outline);
        g.fillRoundedRectangle (track, w * 0.5f);

        auto filled = track.withTop (sliderPos);
        g.setColour (accentColour.withAlpha (0.85f));
        g.fillRoundedRectangle (filled, w * 0.5f);

        g.setColour (accentColour.brighter (0.4f));
        g.fillEllipse (juce::Rectangle<float> (w * 1.9f, w * 1.9f).withCentre ({ cx, sliderPos }));
    }
}

//==============================================================================
void GapFillLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                       int, int, int, int, juce::ComboBox& box)
{
    juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds.reduced (0.5f), 4.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    juce::Path arrow;
    const auto cx = (float) width - 12.0f;
    const auto cy = (float) height * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 3.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);

    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void GapFillLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

//==============================================================================
void GapFillLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour;

    if (down)
        colour = colour.brighter (0.25f);
    else if (highlighted)
        colour = colour.brighter (0.12f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (button.getToggleState() ? colour.brighter (0.5f) : colours::outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

void GapFillLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                         bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0),
                      juce::Justification::centred, 1, 0.8f);
}

void GapFillLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                           bool highlighted, bool)
{
    auto bounds = button.getLocalBounds().toFloat();
    const auto boxSize = juce::jmin (16.0f, bounds.getHeight() - 2.0f);
    juce::Rectangle<float> box (bounds.getX() + 1.0f, bounds.getCentreY() - boxSize * 0.5f, boxSize, boxSize);

    g.setColour (button.getToggleState() ? colours::accent.withAlpha (0.85f) : colours::panelRaised);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (highlighted ? colours::accent : colours::outline);
    g.drawRoundedRectangle (box, 3.0f, 1.0f);

    if (button.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + boxSize * 0.24f, box.getCentreY());
        tick.lineTo (box.getCentreX() - boxSize * 0.03f, box.getBottom() - boxSize * 0.28f);
        tick.lineTo (box.getRight() - boxSize * 0.22f, box.getY() + boxSize * 0.28f);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    g.setColour (button.findColour (juce::ToggleButton::textColourId));
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (button.getButtonText(),
                      bounds.withTrimmedLeft (boxSize + 8.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.8f);
}

//==============================================================================
juce::Font GapFillLookAndFeel::getLabelFont (juce::Label& label)
{
    return juce::Font (juce::FontOptions (juce::jlimit (10.0f, 18.0f, (float) label.getHeight() * 0.72f)));
}

juce::Font GapFillLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (13.0f));
}

juce::Font GapFillLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jlimit (10.0f, 14.0f, (float) buttonHeight * 0.48f)));
}

juce::Font GapFillLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (13.5f));
}

//==============================================================================
void GapFillLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, juce::Font (juce::FontOptions (12.5f)),
              findColour (juce::TooltipWindow::textColourId));
    s.draw (g, bounds.reduced (9.0f, 6.0f));
}

juce::Rectangle<int> GapFillLookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                           juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (tipText, juce::Font (juce::FontOptions (12.5f)));

    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 340.0f);

    const auto w = (int) (layout.getWidth() + 20.0f);
    const auto h = (int) (layout.getHeight() + 14.0f);

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 18,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h)
             .constrainedWithin (parentArea);
}

//==============================================================================
void GapFillLookAndFeel::drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds,
                                    const juce::String& title, juce::Colour tint)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.0f);

    if (title.isNotEmpty())
    {
        auto header = bounds.removeFromTop (24.0f).reduced (10.0f, 0.0f);

        g.setColour (tint);
        g.fillRoundedRectangle (header.getX() - 4.0f, header.getCentreY() - 5.0f, 3.0f, 10.0f, 1.5f);

        g.setColour (colours::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)).withExtraKerningFactor (0.14f));
        g.drawText (title.toUpperCase(), header.toNearestInt(), juce::Justification::centredLeft);
    }
}

} // namespace gapfill
