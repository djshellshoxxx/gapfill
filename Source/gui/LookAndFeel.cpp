#include "LookAndFeel.h"
#include "Knob.h"   // FlashButton::flashProperty - the momentary flash flag

namespace gapfill
{

//==============================================================================
namespace fonts
{
namespace
{
    /** Returns the first name in `chain` that is actually installed, or an empty
        string to mean "let JUCE pick the platform default". Looked up once. */
    juce::String resolve (const juce::StringArray& chain)
    {
        const auto installed = juce::Font::findAllTypefaceNames();

        for (const auto& wanted : chain)
            if (installed.contains (wanted))
                return wanted;

        return {};
    }

    const juce::String& sansName()
    {
        static const juce::String name = resolve ({ "Inter", "Space Grotesk", "Segoe UI Variable Text",
                                                    "Segoe UI", "Helvetica Neue" });
        return name;
    }

    const juce::String& monoName()
    {
        static const juce::String name = resolve ({ "JetBrains Mono", "IBM Plex Mono", "Cascadia Mono",
                                                    "Consolas", "Menlo", "DejaVu Sans Mono" });
        return name;
    }

    juce::FontOptions withFace (const juce::String& face, float height)
    {
        return face.isEmpty() ? juce::FontOptions (height)
                              : juce::FontOptions (face, height, juce::Font::plain);
    }
}

juce::Font ui (float height, bool semiBold)
{
    auto f = juce::Font (withFace (sansName(), height));

    // Weight 600 for headers; the rest of the UI sits at the regular weight,
    // which reads as the spec's 500 at these sizes.
    return semiBold ? f.boldened() : f;
}

juce::Font mono (float height)
{
    return juce::Font (withFace (monoName(), height));
}

juce::Font label (float height)
{
    return ui (height).withExtraKerningFactor (0.08f);
}

juce::Font sectionHeader (float height)
{
    return ui (height, true).withExtraKerningFactor (0.08f);
}

juce::Font value (float height)
{
    return mono (height);
}
} // namespace fonts

namespace
{
    void drawKeyboardFocusRing (juce::Graphics& g,
                                const juce::Component& component,
                                juce::Rectangle<float> bounds,
                                float radius)
    {
        if (! component.hasKeyboardFocus (false))
            return;

        g.setColour (colours::primary.withAlpha (0.95f));
        g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 2.0f);
    }
}

//==============================================================================
GapFillLookAndFeel::GapFillLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::Label::textColourId,                 colours::text);
    setColour (juce::Slider::textBoxTextColourId,         colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::Slider::thumbColourId,               colours::primary);
    setColour (juce::ComboBox::backgroundColourId,        colours::panel);
    setColour (juce::ComboBox::textColourId,              colours::text);
    setColour (juce::ComboBox::outlineColourId,           colours::edge);
    setColour (juce::ComboBox::arrowColourId,             colours::textMuted);
    setColour (juce::PopupMenu::backgroundColourId,       colours::panel);
    setColour (juce::PopupMenu::textColourId,             colours::text);
    setColour (juce::PopupMenu::headerTextColourId,       colours::textMuted);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::primary.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId,  colours::text);
    setColour (juce::TextButton::buttonColourId,          colours::panel);
    setColour (juce::TextButton::buttonOnColourId,        colours::primary);
    setColour (juce::TextButton::textColourOffId,         colours::textMuted);
    setColour (juce::TextButton::textColourOnId,          colours::primary);
    setColour (juce::ToggleButton::textColourId,          colours::text);
    setColour (juce::TooltipWindow::backgroundColourId,   juce::Colour (0xf20e1116));
    setColour (juce::TooltipWindow::textColourId,         colours::textMuted);
    setColour (juce::TooltipWindow::outlineColourId,      colours::edge);
    setColour (juce::ScrollBar::thumbColourId,            colours::edge.brighter (0.4f));
    setColour (juce::AlertWindow::backgroundColourId,     colours::panel);
    setColour (juce::AlertWindow::textColourId,           colours::text);
    setColour (juce::AlertWindow::outlineColourId,        colours::edge);
    setColour (juce::TextEditor::backgroundColourId,      colours::background);
    setColour (juce::TextEditor::textColourId,            colours::text);
    setColour (juce::TextEditor::outlineColourId,         colours::edge);
    setColour (juce::TextEditor::focusedOutlineColourId,  colours::primary);
    setColour (juce::TextEditor::highlightColourId,       colours::primary.withAlpha (0.3f));
    setColour (juce::CaretComponent::caretColourId,       colours::primary);
}

//==============================================================================
juce::ColourGradient GapFillLookAndFeel::bodyGradient (juce::Rectangle<float> bounds)
{
    return juce::ColourGradient (colours::knobTop,    bounds.getCentreX(), bounds.getY(),
                                 colours::knobBottom, bounds.getCentreX(), bounds.getBottom(),
                                 false);
}

void GapFillLookAndFeel::drawShadow (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
{
    // Cheap approximation of an 8px blur with a +2 y offset: a few expanding,
    // fading rings. Far cheaper than a real blur and visually equivalent here.
    for (int i = 4; i >= 1; --i)
    {
        const auto spread = (float) i * 2.0f;
        g.setColour (colours::shadow.withMultipliedAlpha (0.10f));
        g.fillRoundedRectangle (bounds.expanded (spread).translated (0.0f, 2.0f),
                                cornerRadius + spread);
    }
}

//==============================================================================
void GapFillLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                           juce::Slider& slider)
{
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto full = juce::jmin (area.getWidth(), area.getHeight());

    // Spec geometry: a 3px value arc sits OUTSIDE the knob body with a 4px gap.
    // Everything scales from the available size so the knob still works at the
    // 36 / 48 / 64px sizes and when the window is resized.
    const auto scale     = juce::jlimit (0.7f, 1.6f, full / (float) metrics::knobDefault);
    const auto arcWidth  = 3.0f * scale;
    const auto gap       = 4.0f * scale;

    const auto arcRadius  = full * 0.5f - arcWidth * 0.5f;
    const auto bodyRadius = juce::jmax (4.0f, arcRadius - arcWidth * 0.5f - gap);
    const auto centre     = area.getCentre();

    const auto enabled = slider.isEnabled();
    const auto accent  = slider.findColour (juce::Slider::thumbColourId);
    const auto angle   = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Hover brightens the whole control by ~8%.
    const auto hovered = slider.isMouseOverOrDragging();
    const auto lift    = hovered ? 0.08f : 0.0f;

    // --- value arc ---------------------------------------------------------
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (colours::edge.withAlpha (0.6f));
        g.strokePath (track, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        if (enabled && std::abs (angle - rotaryStartAngle) > 1.0e-4f)
        {
            juce::Path filled;
            filled.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                  rotaryStartAngle, angle, true);
            g.setColour (accent.brighter (lift));
            g.strokePath (filled, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }
    }

    // --- C-0 effective value: a thin inner arc from the base value to the value
    // the engine really uses, plus a radial tick on the value arc (shape, not
    // colour alone). Only drawn when GapKnob::setEffectiveValue supplied one.
    if (enabled)
    {
        const auto eff = (double) slider.getProperties().getWithDefault ("gapfillEffective01", -1.0);

        if (eff >= 0.0 && eff <= 1.0)
        {
            const auto effAngle = rotaryStartAngle + (float) eff * (rotaryEndAngle - rotaryStartAngle);
            const auto innerRadius = juce::jmax (4.0f, arcRadius - arcWidth * 0.5f - gap * 0.5f);

            juce::Path delta;
            delta.addCentredArc (centre.x, centre.y, innerRadius, innerRadius, 0.0f,
                                 juce::jmin (angle, effAngle), juce::jmax (angle, effAngle), true);
            g.setColour (colours::text.withAlpha (0.85f));
            g.strokePath (delta, juce::PathStrokeType (1.5f * scale, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::butt));

            juce::Path tick;
            tick.startNewSubPath (0.0f, -(arcRadius - arcWidth * 1.1f));
            tick.lineTo (0.0f, -(arcRadius + arcWidth * 0.9f));
            tick.applyTransform (juce::AffineTransform::rotation (effAngle).translated (centre));
            g.setColour (colours::text);
            g.strokePath (tick, juce::PathStrokeType (1.5f * scale));
        }
    }

    // --- body --------------------------------------------------------------
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    {
        auto gradient = bodyGradient (body);

        if (hovered)
        {
            gradient.clearColours();
            gradient.addColour (0.0, colours::knobTop.brighter (lift));
            gradient.addColour (1.0, colours::knobBottom.brighter (lift));
        }

        g.setGradientFill (gradient);
        g.fillEllipse (body);

        g.setColour (colours::edge);
        g.drawEllipse (body.reduced (0.5f), 1.0f);
    }

    // --- indicator: a single 2px line from centre to rim, rounded cap -------
    if (enabled)
    {
        const auto indicatorWidth = 2.0f * scale;
        const auto inner = bodyRadius * 0.28f;
        const auto outer = bodyRadius * 0.86f;

        juce::Path indicator;
        indicator.startNewSubPath (0.0f, -inner);
        indicator.lineTo (0.0f, -outer);
        indicator.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));

        g.setColour (accent.brighter (lift));
        g.strokePath (indicator, juce::PathStrokeType (indicatorWidth, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
    }

    // --- centre dot: accent when moved from default, muted when at default --
    {
        const auto dotSize = 4.0f * scale;
        const auto atDefault = slider.isEnabled()
                                 && std::abs (slider.getValue() - slider.getDoubleClickReturnValue()) < 1.0e-6;

        g.setColour (atDefault || ! enabled ? colours::textMuted.withAlpha (0.5f)
                                            : accent.brighter (lift));
        g.fillEllipse (juce::Rectangle<float> (dotSize, dotSize).withCentre (centre));
    }

    drawKeyboardFocusRing (g, slider, area, juce::jmin (8.0f, full * 0.18f));
}

void GapFillLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float, float,
                                           juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto accent  = slider.findColour (juce::Slider::thumbColourId);
    const auto hovered = slider.isMouseOverOrDragging();
    const auto lift    = hovered ? 0.08f : 0.0f;

    constexpr float trackThickness = 4.0f;   // spec: 4px track
    constexpr float thumbShort     = 16.0f;  // spec: 16x24 rounded rect
    constexpr float thumbLong      = 24.0f;

    // Spec: 1px muted tick lines at a logical interval, outside the track.
    // Quarters are the sensible generic reading of "logical interval" - these
    // sliders carry percentages and amounts rather than decibel scales.
    constexpr int   numTickDivisions = 4;
    constexpr float tickLength       = 3.0f;
    constexpr float tickGap          = 3.0f;

    if (style == juce::Slider::LinearHorizontal)
    {
        const auto cy = (float) y + (float) height * 0.5f;
        juce::Rectangle<float> track ((float) x, cy - trackThickness * 0.5f,
                                      (float) width, trackThickness);

        g.setColour (colours::edge);
        g.fillRoundedRectangle (track, trackThickness * 0.5f);

        auto filled = track.withWidth (juce::jmax (0.0f, sliderPos - (float) x));
        g.setColour (accent.brighter (lift));
        g.fillRoundedRectangle (filled, trackThickness * 0.5f);

        g.setColour (colours::textMuted.withAlpha (0.45f));

        for (int i = 0; i <= numTickDivisions; ++i)
        {
            const auto tx = std::round ((float) x + (float) width * (float) i / (float) numTickDivisions);
            const auto top = track.getBottom() + tickGap;
            g.fillRect (juce::Rectangle<float> (tx, top, 1.0f, tickLength));
        }

        auto thumb = juce::Rectangle<float> (thumbShort, juce::jmin (thumbLong, (float) height))
                       .withCentre ({ sliderPos, cy });

        g.setGradientFill (bodyGradient (thumb));
        g.fillRoundedRectangle (thumb, metrics::controlRadius);
        g.setColour (accent.brighter (lift));
        g.drawRoundedRectangle (thumb.reduced (0.5f), metrics::controlRadius, 1.0f);
    }
    else
    {
        const auto cx = (float) x + (float) width * 0.5f;
        juce::Rectangle<float> track (cx - trackThickness * 0.5f, (float) y,
                                      trackThickness, (float) height);

        g.setColour (colours::edge);
        g.fillRoundedRectangle (track, trackThickness * 0.5f);

        auto filled = track.withTop (sliderPos);
        g.setColour (accent.brighter (lift));
        g.fillRoundedRectangle (filled, trackThickness * 0.5f);

        g.setColour (colours::textMuted.withAlpha (0.45f));

        for (int i = 0; i <= numTickDivisions; ++i)
        {
            const auto ty = std::round ((float) y + (float) height * (float) i / (float) numTickDivisions);
            const auto left = track.getRight() + tickGap;
            g.fillRect (juce::Rectangle<float> (left, ty, tickLength, 1.0f));
        }

        auto thumb = juce::Rectangle<float> (juce::jmin (thumbLong, (float) width), thumbShort)
                       .withCentre ({ cx, sliderPos });

        g.setGradientFill (bodyGradient (thumb));
        g.fillRoundedRectangle (thumb, metrics::controlRadius);
        g.setColour (accent.brighter (lift));
        g.drawRoundedRectangle (thumb.reduced (0.5f), metrics::controlRadius, 1.0f);
    }

    drawKeyboardFocusRing (g, slider,
                           juce::Rectangle<float> ((float) x, (float) y,
                                                   (float) width, (float) height),
                           metrics::controlRadius);
}

//==============================================================================
void GapFillLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                       int, int, int, int, juce::ComboBox& box)
{
    juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);
    const auto hovered = box.isMouseOver();

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId).brighter (hovered ? 0.08f : 0.0f));
    g.fillRoundedRectangle (bounds.reduced (0.5f), metrics::panelRadius);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), metrics::panelRadius, 1.0f);
    drawKeyboardFocusRing (g, box, bounds, metrics::panelRadius);

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
    label.setBounds (metrics::grid, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

//==============================================================================
void GapFillLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto on = button.getToggleState();

    // Spec: OFF is the panel surface; ON is the accent at 15% with an accent
    // border. A momentary press also flashes the accent for 100ms - the button
    // sets this property for the duration, see FlashButton.
    const auto accent = on ? backgroundColour : colours::primary;
    const bool flashing = (bool) button.getProperties()
                                   .getWithDefault (FlashButton::flashProperty, false);

    if (on)
    {
        g.setColour (accent.withAlpha (down || flashing ? 0.30f : (highlighted ? 0.22f : 0.15f)));
        g.fillRoundedRectangle (bounds, metrics::panelRadius);
        g.setColour (accent);
        g.drawRoundedRectangle (bounds, metrics::panelRadius, 1.0f);
    }
    else
    {
        auto surface = colours::panel;

        if (down || flashing)
            surface = accent.withAlpha (0.25f).overlaidWith (surface).brighter (0.20f);
        else if (highlighted)
            surface = surface.brighter (0.08f);

        g.setColour (surface);
        g.fillRoundedRectangle (bounds, metrics::panelRadius);
        g.setColour (down || flashing ? accent : colours::edge);
        g.drawRoundedRectangle (bounds, metrics::panelRadius, 1.0f);
    }

    drawKeyboardFocusRing (g, button, bounds, metrics::panelRadius);
}

void GapFillLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                         bool highlighted, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));

    auto colour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                             : juce::TextButton::textColourOffId);

    if (highlighted)
        colour = colour.brighter (0.08f);

    g.setColour (button.isEnabled() ? colour : colour.withAlpha (0.4f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (metrics::halfGrid, 0),
                      juce::Justification::centred, 1, 0.85f);
}

void GapFillLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                           bool highlighted, bool)
{
    auto bounds = button.getLocalBounds().toFloat();
    const auto boxSize = juce::jmin (16.0f, bounds.getHeight() - 2.0f);
    juce::Rectangle<float> box (bounds.getX() + 1.0f, bounds.getCentreY() - boxSize * 0.5f, boxSize, boxSize);
    const auto on = button.getToggleState();

    g.setColour (on ? colours::primary.withAlpha (0.15f) : colours::panel);
    g.fillRoundedRectangle (box, metrics::controlRadius);
    g.setColour (on ? colours::primary : (highlighted ? colours::edge.brighter (0.3f) : colours::edge));
    g.drawRoundedRectangle (box, metrics::controlRadius, 1.0f);

    if (on)
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + boxSize * 0.24f, box.getCentreY());
        tick.lineTo (box.getCentreX() - boxSize * 0.03f, box.getBottom() - boxSize * 0.28f);
        tick.lineTo (box.getRight() - boxSize * 0.22f, box.getY() + boxSize * 0.28f);
        g.setColour (colours::primary);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    drawKeyboardFocusRing (g, button, bounds, metrics::controlRadius);

    g.setColour (on ? colours::primary : colours::textMuted);
    g.setFont (fonts::label());
    g.drawFittedText (button.getButtonText().toUpperCase(),
                      bounds.withTrimmedLeft (boxSize + (float) metrics::grid).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.85f);
}

//==============================================================================
juce::Font GapFillLookAndFeel::getLabelFont (juce::Label& label)
{
    return fonts::ui (juce::jlimit (10.0f, 18.0f, (float) label.getHeight() * 0.72f));
}

juce::Font GapFillLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return fonts::ui (13.0f);
}

juce::Font GapFillLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return fonts::label (juce::jlimit (9.5f, 12.0f, (float) buttonHeight * 0.42f));
}

juce::Font GapFillLookAndFeel::getPopupMenuFont()
{
    return fonts::ui (13.5f);
}

juce::Font GapFillLookAndFeel::getAlertWindowMessageFont()
{
    return fonts::ui (13.5f);
}

juce::Font GapFillLookAndFeel::getAlertWindowTitleFont()
{
    return fonts::ui (16.0f, true);
}

//==============================================================================
void GapFillLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

    // Spec: a dark pill with muted text.
    const auto radius = juce::jmin (bounds.getHeight() * 0.5f, 10.0f);

    g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);

    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, fonts::ui (12.0f), findColour (juce::TooltipWindow::textColourId));
    s.draw (g, bounds.reduced (12.0f, 6.0f));
}

juce::Rectangle<int> GapFillLookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                           juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (tipText, fonts::ui (12.0f));

    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 340.0f);

    const auto w = (int) (layout.getWidth() + 26.0f);
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
    g.fillRoundedRectangle (bounds, metrics::panelRadius);
    g.setColour (colours::edge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), metrics::panelRadius, 1.0f);

    if (title.isNotEmpty())
    {
        auto header = bounds.removeFromTop (24.0f).withTrimmedLeft ((float) metrics::edgePadding);

        // Spec: a 2px wide, 12px tall accent bar to the left of the header.
        g.setColour (tint);
        g.fillRect (juce::Rectangle<float> (2.0f, 12.0f)
                      .withCentre ({ header.getX() - 6.0f, header.getCentreY() }));

        g.setColour (colours::text);
        g.setFont (fonts::sectionHeader());
        g.drawText (title.toUpperCase(), header.toNearestInt(), juce::Justification::centredLeft);
    }
}

void GapFillLookAndFeel::drawSeparator (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (colours::edge);
    g.fillRect (area.withHeight (1.0f).withY (area.getCentreY()));
}

void GapFillLookAndFeel::drawSignatureNotch (juce::Graphics& g, juce::Rectangle<float> windowBounds)
{
    // 12px long, 45 degrees, 2px wide, in the top-left corner.
    constexpr float length = 12.0f;
    constexpr float inset  = 6.0f;

    const auto diagonal = length * juce::MathConstants<float>::sqrt2 * 0.5f;

    juce::Path notch;
    notch.startNewSubPath (windowBounds.getX() + inset, windowBounds.getY() + inset + diagonal);
    notch.lineTo (windowBounds.getX() + inset + diagonal, windowBounds.getY() + inset);

    g.setColour (colours::primary);
    g.strokePath (notch, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::butt));
}

} // namespace gapfill
