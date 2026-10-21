#include "Gui.h"

namespace gapfill::reasons
{

const std::array<LegendEntry, 7>& legendEntries() noexcept
{
    static const std::array<LegendEntry, 7> entries =
    {{
        { "EVENT",     "Event: a detected hit, drawn as a white vertical bar",           Pattern::VerticalBar },
        { "GAP",       "Gap eligible: room where a fill could go, drawn as a teal tint",  Pattern::Tint },
        { "FILL",      "Fill: a generated fill, drawn as a solid orange bar",             Pattern::SolidBar },
        { "PROTECTED", "Protected: time held back after a hit, drawn as an amber tick",   Pattern::TopTick },
        { "LOOKAHEAD", "Lookahead: the window before the next hit, drawn with a dashed edge", Pattern::DashedEdge },
        { "PREVIEW",   "Preview: an audition fill, drawn hatched",                        Pattern::Hatch },
        { "SKIPPED",   "Skipped: a gap that was not filled, drawn as a hollow diamond",   Pattern::HollowDiamond },
    }};

    return entries;
}

juce::Colour swatchColour (Pattern p) noexcept
{
    switch (p)
    {
        case Pattern::VerticalBar:   return juce::Colours::white;
        case Pattern::Tint:          return colours::accent;
        case Pattern::SolidBar:      return colours::accentWarm;
        case Pattern::TopTick:       return colours::accentAmber;
        case Pattern::DashedEdge:    return colours::accent;
        case Pattern::Hatch:         return colours::accentWarm;
        case Pattern::HollowDiamond: return colours::text;
    }

    return colours::text;
}

void drawSwatch (juce::Graphics& g, juce::Rectangle<float> r, Pattern p, juce::Colour c)
{
    g.setColour (c);

    switch (p)
    {
        case Pattern::VerticalBar:
            g.fillRect (r.getCentreX() - 1.0f, r.getY(), 2.0f, r.getHeight());
            break;

        case Pattern::Tint:
            g.setColour (c.withAlpha (0.35f));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (c.withAlpha (0.8f));
            g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
            break;

        case Pattern::SolidBar:
            g.fillRoundedRectangle (r.withSizeKeepingCentre (r.getWidth(), r.getHeight() * 0.6f), 2.0f);
            break;

        case Pattern::TopTick:
            g.fillRect (r.getX(), r.getY(), r.getWidth(), 3.0f);
            g.setColour (c.withAlpha (0.25f));
            g.fillRect (r.getX(), r.getY() + 3.0f, r.getWidth(), r.getHeight() - 3.0f);
            break;

        case Pattern::DashedEdge:
        {
            const float dashes[] = { 3.0f, 2.0f };
            g.drawDashedLine ({ r.getX(), r.getBottom() - 1.0f, r.getRight(), r.getBottom() - 1.0f }, dashes, 2, 1.2f);
            g.drawDashedLine ({ r.getX(), r.getY() + 1.0f, r.getRight(), r.getY() + 1.0f }, dashes, 2, 1.2f);
            break;
        }

        case Pattern::Hatch:
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (r.toNearestInt());

            for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 4.0f)
                g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 1.0f);

            break;
        }

        case Pattern::HollowDiamond:
        {
            juce::Path d;
            const auto cx = r.getCentreX(), cy = r.getCentreY(), h = juce::jmin (r.getHeight(), r.getWidth()) * 0.5f;
            d.addQuadrilateral (cx, cy - h, cx + h, cy, cx, cy + h, cx - h, cy);
            g.strokePath (d, juce::PathStrokeType (1.3f));
            break;
        }
    }
}

//==============================================================================
LegendStrip::LegendStrip()
{
    setTitle ("Visualiser legend");
    setDescription ("Shows how each state in the gap view is drawn. Press Enter to collapse or expand.");
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    juce::String all;

    for (const auto& e : legendEntries())
        all << e.description << ". ";

    setHelpText (all);
}

void LegendStrip::setCollapsed (bool shouldCollapse)
{
    if (collapsed != shouldCollapse)
    {
        collapsed = shouldCollapse;
        repaint();
    }
}

int LegendStrip::getPreferredWidth() const
{
    if (collapsed)
        return 74;

    int w = 78;
    const auto font = fonts::label (9.5f);

    for (const auto& e : legendEntries())
        w += 20 + (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, e.label)) + 14;

    return w;
}

void LegendStrip::toggle()
{
    setCollapsed (! collapsed);

    if (onCollapsedChanged != nullptr)
        onCollapsedChanged (collapsed);
}

void LegendStrip::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()))
        toggle();
}

bool LegendStrip::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey)
    {
        toggle();
        return true;
    }

    return false;
}

void LegendStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (colours::background.withAlpha (0.78f));
    g.fillRoundedRectangle (b, 3.0f);

    g.setFont (fonts::label (9.5f));
    g.setColour (colours::textDim);
    g.drawText (collapsed ? "LEGEND  +" : "LEGEND  -", b.withWidth (70.0f).reduced (6.0f, 0.0f),
                juce::Justification::centredLeft, false);

    if (collapsed)
        return;

    auto x = b.getX() + 78.0f;
    const auto font = fonts::label (9.5f);

    for (const auto& e : legendEntries())
    {
        const auto entryWidth = 20.0f + (float) std::ceil (juce::GlyphArrangement::getStringWidth (font, e.label)) + 2.0f;

        if (x + entryWidth > b.getRight())
            break;                                   // narrow: show the entries that fit

        const auto swatch = juce::Rectangle<float> (x, b.getCentreY() - 5.0f, 14.0f, 10.0f);
        drawSwatch (g, swatch, e.pattern, swatchColour (e.pattern));

        g.setColour (colours::textDim);
        g.setFont (font);
        const auto w = (float) std::ceil (juce::GlyphArrangement::getStringWidth (font, e.label)) + 2.0f;
        g.drawText (e.label, juce::Rectangle<float> (x + 18.0f, b.getY(), w, b.getHeight()),
                    juce::Justification::centredLeft, false);
        x += 20.0f + w + 14.0f;
    }
}

} // namespace gapfill::reasons
